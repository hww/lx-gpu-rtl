`timescale 1ns/1ps
`default_nettype none

// top: LX-GPU core.
//
// Operand map (6 bits):
//   0..31   R0..R31
//   32      ZERO
//   33      FLAGS
//   34      BUSY
//   35      SHADOW
//   36      LOOP_CNT
//   37      LOOP_TGT
//   38      ROML    (R/W, no ROMA++)
//   39      ROMH    (R/W, ROMA++ on access)
//   40      ROMA    (W only)
//   41      MHI
//   42..43  reserved
//   44..47  DATA[0..3]
//   48..51  IDX[0..3]
//   52..55  IDXH[0..3]
//   56..59  STEP[0..3]
//   60..63  PAGE[0..3]
//
// ROM protocol:
//   MOV ROMA, R      -> set address
//   MOV ROML, R      -> low 16 bits (no increment)
//   MOV ROMH, R      -> high 16 bits, ROMA++
//   MOV Rd, ROML     -> read low 16 (no increment)
//   MOV Rd, ROMH     -> read high 16, ROMA++
//
// ack: any instruction waits for ack. Formed per-instruction:
//   DATA read   -> pb_ack
//   ROM read    -> rom_wait (1 cycle for BRAM port A)
//   DATA write  -> gc_access_done
//   everything else -> 1
module top (
    input  logic        clk,
    input  logic        rst,
    output logic        halted,
    output logic        error,
    output logic [11:0] error_code,
    input  logic        start,
    input  logic        gpu_start,
    input  logic        gpu_reset,
    input  logic [23:0] gpu_addr,

    output logic        wb_cyc,
    output logic        wb_stb,
    output logic        wb_we,
    output logic [23:0] wb_adr,
    output logic [15:0] wb_dat_o,
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack
);

    // ===== ROM =====
    logic [11:0] pc;
    logic [31:0] insn;
    logic        ack;

    // port A (data access from ISA)
    logic        rom_a_we, rom_a_re;
    logic [11:0] rom_a_addr;
    logic [31:0] rom_a_wdata;
    logic [31:0] rom_a_rdata;

    // ROMA register (maintained by top)
    logic [11:0] roma;

    // external load mux (simulation)
    logic        rom_a_we_mux;
    logic [11:0] rom_a_addr_mux;
    logic [31:0] rom_a_wdata_mux;

    assign rom_a_we_mux    = rom_a_we;
    assign rom_a_addr_mux  = rom_a_addr;
    assign rom_a_wdata_mux = rom_a_wdata;

    microcode_rom #(.DEPTH(4096), .ADDR_W(12), .DATA_W(32),
                    .INIT_FILE("rtl/bootloader.hex")) rom (
        .clk(clk), .rst(rst),
        .addr(pc), .data(insn), .ack(ack),
        .we(rom_a_we), .waddr(rom_a_addr), .wdata(rom_a_wdata),
        .rdata_a(rom_a_rdata), .re(rom_a_re)
    );

    // ===== decode =====
    logic        l;
    logic [1:0]  imm_class;
    logic [5:0]  dst_code;
    logic        write_en;
    logic [3:0]  cond;
    logic        f;
    logic [4:0]  reg_op;
    logic        r_is_reg;
    logic [5:0]  src;
    logic        sx;
    logic [10:0] imm11;
    logic [11:0] imm12_ctrl;
    logic [15:0] imm16;
    logic        is_ldi, is_ctrl;

    assign l          = insn[31];
    assign imm_class  = {insn[30], insn[29]};
    assign dst_code   = insn[28:23];
    assign write_en   = insn[22];
    assign cond       = insn[21:18];
    assign f          = insn[17];
    assign reg_op     = insn[16:12];
    assign r_is_reg   = insn[16];
    assign src        = insn[11:6];
    assign sx         = insn[11];
    assign imm11      = insn[10:0];
    assign imm12_ctrl = insn[11:0];
    assign imm16      = insn[15:0];
    assign is_ldi     = (imm_class == 2'b01);
    assign is_ctrl    = (imm_class == 2'b10);

    logic skip_insn;

    // ===== flags =====
    logic z_r, n_r, c_r, v_r;
    logic z, n, c, v;
    assign z = z_r; assign n = n_r; assign c = c_r; assign v = v_r;

    logic alu_z, alu_n, alu_c, alu_v;
    logic [15:0] alu_mhi;

    logic cond_ok;
    always_comb begin
        case (cond)
            4'd0:  cond_ok = 1'b1;
            4'd1:  cond_ok = z_r;
            4'd2:  cond_ok = ~z_r;
            4'd3:  cond_ok = c_r;
            4'd4:  cond_ok = ~c_r;
            4'd5:  cond_ok = n_r;
            4'd6:  cond_ok = ~n_r;
            4'd7:  cond_ok = v_r;
            4'd8:  cond_ok = ~v_r;
            4'd9:  cond_ok = c_r & ~z_r;
            4'd10: cond_ok = ~c_r | z_r;
            4'd11: cond_ok = (n_r == v_r);
            4'd12: cond_ok = (n_r != v_r);
            4'd13: cond_ok = ~z_r & (n_r == v_r);
            4'd14: cond_ok = z_r | (n_r != v_r);
            default: cond_ok = 1'b0;
        endcase
    end

    logic exec, run;
    assign exec = ack && !skip_insn && !halted && !error;
    // run: side-effect launch (no ack dependency) — breaks ack<->exec<->gc_access loop
    assign run  = !skip_insn && !halted && !error;

    // ===== regfile =====
    logic [4:0]  ra, rb, wa;
    logic [15:0] rdata_a, rdata_b, wdata;
    logic        we_rf;
    regfile rf (
        .clk(clk), .rst(rst),
        .ra(ra), .rdata_a(rdata_a),
        .rb(rb), .rdata_b(rdata_b),
        .we(we_rf), .wa(wa), .wdata(wdata)
    );

    // ===== ALU =====
    logic [4:0]  alu_op;
    logic [15:0] alu_a, alu_b, alu_res;
    alu al (
        .clk(clk), .rst(rst),
        .op(alu_op), .a(alu_a), .b(alu_b), .c_in(c_r),
        .res(alu_res), .mhi(alu_mhi),
        .z(alu_z), .n(alu_n), .c(alu_c), .v(alu_v)
    );

    // ===== sequencer =====
    logic [15:0] shadow, loop_cnt;
    logic [11:0] loop_tgt;
    logic        irq_pending;
    logic [11:0] irq_code, jmpr_target;
    logic        loop_cnt_we, loop_tgt_we, shadow_we;
    logic [15:0] loop_cnt_in, shadow_in;
    logic [11:0] loop_tgt_in;

    sequencer seq (
        .clk(clk), .rst(rst),
        .start(start | gpu_start),
        .reset_halt(gpu_reset),
        .imm_class(imm_class), .reg_op(reg_op), .cond(cond),
        .l(l), .imm12(imm12_ctrl), .r_is_reg(r_is_reg),
        .z(z_r), .n(n_r), .c(c_r), .v(v_r),
        .jmpr_target(jmpr_target),
        .pc(pc), .shadow(shadow),
        .loop_cnt(loop_cnt), .loop_tgt(loop_tgt),
        .halted(halted), .error(error), .error_code(error_code),
        .irq_pending(irq_pending), .irq_code(irq_code),
        .skip_insn(skip_insn),
        .ack(ack),
        .loop_cnt_we(loop_cnt_we), .loop_cnt_in(loop_cnt_in),
        .loop_tgt_we(loop_tgt_we), .loop_tgt_in(loop_tgt_in),
        .shadow_we(shadow_we), .shadow_in(shadow_in)
    );

    // ===== pixel_engine =====
    logic [1:0]  pe_ch;
    logic        pe_mode_we, pe_idxh_we, pe_idx_we, pe_step_we;
    logic [2:0]  pe_mode_in, pe_idxh_in;
    logic [15:0] pe_idx_in, pe_step_in;
    logic        pe_access;
    logic [14:0] pe_word_out;
    logic [3:0]  pe_pos;
    logic [2:0]  pe_mode;
    logic        pe_prefetch_req;

    pixel_engine pe (
        .clk(clk), .rst(rst),
        .ch(pe_ch),
        .mode_we(pe_mode_we), .mode_in(pe_mode_in),
        .idxh_we(pe_idxh_we), .idxh_in(pe_idxh_in),
        .idx_we(pe_idx_we),   .idx_in(pe_idx_in),
        .step_we(pe_step_we), .step_in(pe_step_in),
        .access(pe_access),
        .access_done(pb_ack),
        .word_out(pe_word_out),
        .pos(pe_pos), .mode(pe_mode),
        .prefetch_req(pe_prefetch_req)
    );

    // ===== gpu_cache =====
    logic        gc_page_we;
    logic [7:0]  gc_page_in, gc_page_out;
    logic        gc_flush_en, gc_flush_done;
    logic [1:0]  gc_flush_ch;
    logic        gc_access, gc_we;
    logic [15:0] gc_wdata;
    logic        gc_hit;
    logic [15:0] gc_rdata;
    logic        gc_access_done;
    logic        gc_wb_cyc, gc_wb_stb, gc_wb_we;
    logic [23:0] gc_wb_adr;
    logic [15:0] gc_wb_dat_o;
    logic [23:0] gc_addr24;
    logic        gc_error;

    gpu_cache gc (
        .clk(clk), .rst(rst),
        .ch(pe_ch),
        .word_in(pe_word_out),
        .access(gc_access),
        .prefetch_req(pe_prefetch_req),
        .page_we(gc_page_we), .page_in(gc_page_in), .page_out(gc_page_out),
        .flush_en(gc_flush_en), .flush_ch(gc_flush_ch), .flush_done(gc_flush_done),
        .we(gc_we), .wdata(gc_wdata),
        .hit(gc_hit), .rdata(gc_rdata), .access_done(gc_access_done),
        .wb_cyc_o(gc_wb_cyc), .wb_stb_o(gc_wb_stb), .wb_we_o(gc_wb_we),
        .wb_adr_o(gc_wb_adr), .wb_dat_o(gc_wb_dat_o),
        .wb_ack_i(wb_ack), .wb_dat_i(wb_dat_i),
        .addr24(gc_addr24), .error(gc_error)
    );

    // ===== pixel_blender =====
    logic        pb_start, pb_read;
    logic [2:0]  pb_mode;
    logic [3:0]  pb_pos;
    logic [15:0] pb_word_in, pb_pixel_in;
    logic [15:0] pb_pixel_out, pb_word_out;
    logic        pb_ack, pb_busy;

    pixel_blender pb (
        .clk(clk), .rst(rst),
        .start(pb_start),
        .read(pb_read), .mode(pb_mode), .pos(pb_pos),
        .word_in(pb_word_in), .pixel_in(pb_pixel_in),
        .pixel_out(pb_pixel_out), .word_out(pb_word_out),
        .ack(pb_ack), .busy(pb_busy)
    );

    assign pb_mode    = pe_mode;
    assign pb_pos     = pe_pos;
    assign pb_word_in = gc_rdata;
    assign pb_start   = gc_access_done;

    logic [15:0] pb_pixel_in_r;
    always_ff @(posedge clk) begin
        if (rst) pb_pixel_in_r <= 16'h0;
        else if (gc_access && gc_we) pb_pixel_in_r <= alu_res;
    end
    assign pb_pixel_in = pb_pixel_in_r;

    // ===== WB =====
    assign wb_cyc   = gc_wb_cyc;
    assign wb_stb   = gc_wb_stb;
    assign wb_we    = gc_wb_we;
    assign wb_adr   = gc_wb_adr;
    assign wb_dat_o = gc_wb_dat_o;

    // ===== helpers =====
    logic is_mov, is_flush;
    assign is_mov   = (reg_op == 5'b00000) && !is_ctrl && !is_ldi;
    assign is_flush = is_ctrl && (reg_op == 5'd6);

    assign ra    = dst_code[4:0];
    assign alu_a = rdata_a;

    // ===== special read =====
    logic [15:0] sp_rd;
    always_comb begin
        sp_rd = 16'h0;
        case (src)
            6'd32: sp_rd = 16'h0;
            6'd33: sp_rd = {12'h0, v_r, c_r, n_r, z_r};
            6'd34: sp_rd = {12'h0, gc_wb_cyc, gc_flush_done,
                            gc_access_done, gc_hit};
            6'd35: sp_rd = shadow;
            6'd36: sp_rd = loop_cnt;
            6'd37: sp_rd = {4'h0, loop_tgt};
            6'd38: sp_rd = rom_a_rdata[15:0];
            6'd39: sp_rd = rom_a_rdata[31:16];
            6'd40: sp_rd = 16'h0;
            6'd41: sp_rd = alu_mhi;
            6'd44, 6'd45, 6'd46, 6'd47: sp_rd = pb_pixel_out;
            default: sp_rd = 16'h0;
        endcase
    end

    // ===== operand B =====
    assign rb    = (imm_class == 2'b00 || (is_ctrl && r_is_reg && src < 6'd32))
                   ? src[4:0] : 5'd0;
    assign alu_b = (imm_class == 2'b11)
                   ? (sx && imm11[10] ? {5'h1F, imm11} : {5'h00, imm11})
                   : (src < 6'd32 ? rdata_b : sp_rd);

    assign alu_op      = reg_op;
    assign jmpr_target = src < 6'd32 ? rdata_b[11:0] : sp_rd[11:0];

    // ===== read/write classification =====
    logic is_data_read, is_rom_read, is_data_write;
    assign is_data_read  = !is_ctrl && (src >= 6'd44 && src < 6'd48);
    assign is_rom_read   = !is_ctrl && (src == 6'd38 || src == 6'd39);
    assign is_data_write = is_mov && (dst_code >= 6'd44 && dst_code < 6'd48);

    // ===== ROM access control =====
    logic [15:0] rom_lo_r;
    logic [31:0] rom_wdata_c;
    logic        rom_wait_r;

    always_comb begin
        rom_wdata_c = {16'h0, rom_lo_r};
        if (exec && cond_ok && is_mov && (dst_code == 6'd39))
            rom_wdata_c[31:16] = alu_res;
    end

    assign rom_a_we    = exec && cond_ok && is_mov && (dst_code == 6'd39);
    assign rom_a_addr  = roma;
    assign rom_a_wdata = rom_wdata_c;
    assign rom_a_re    = is_rom_read && exec;

    always_ff @(posedge clk) begin
        if (rst) begin
            roma    <= 12'h0;
            rom_lo_r<= 16'h0;
        end else begin
            if (exec && cond_ok && is_mov && (dst_code == 6'd40))
                roma <= alu_res[11:0];
            if (exec && cond_ok && is_mov && (dst_code == 6'd38))
                rom_lo_r <= alu_res;
            if (exec && cond_ok && is_mov && (dst_code == 6'd39))
                roma <= roma + 12'd1;
            if (exec && cond_ok && is_rom_read && (src == 6'd39))
                roma <= roma + 12'd1;
        end
    end

    always_ff @(posedge clk) begin
        if (rst) rom_wait_r <= 1'b0;
        else     rom_wait_r <= is_rom_read && exec;
    end

    // ===== ack =====
    assign ack = is_data_read  ? pb_ack
               : is_rom_read   ? rom_wait_r
               : is_data_write ? gc_access_done
               : 1'b1;

    // ===== writeback =====
    assign we_rf = exec && cond_ok && write_en && (dst_code < 6'd32);
    assign wa    = dst_code[4:0];
    assign wdata = is_ldi        ? imm16
                 : is_data_read  ? pb_pixel_out
                 : is_rom_read   ? sp_rd
                 : alu_res;

    // ===== sysreg writes =====
    logic sys_we;
    assign sys_we = exec && cond_ok && write_en && !is_ctrl
                    && ((dst_code >= 6'd33 && dst_code <= 6'd37)
                     || (dst_code >= 6'd44));

    assign shadow_we   = sys_we && (dst_code == 6'd35);
    assign shadow_in   = alu_res;
    assign loop_cnt_we = sys_we && (dst_code == 6'd36);
    assign loop_cnt_in = alu_res;
    assign loop_tgt_we = sys_we && (dst_code == 6'd37);
    assign loop_tgt_in = alu_res[11:0];

    always_ff @(posedge clk) begin
        if (rst) begin
            z_r <= 1'b0; n_r <= 1'b0; c_r <= 1'b0; v_r <= 1'b0;
        end else if (exec && cond_ok && write_en && !is_ctrl &&
                     dst_code == 6'd33) begin
            {v_r, c_r, n_r, z_r} <= alu_res[3:0];
        end else if (exec && f && !is_ctrl && !is_ldi) begin
            z_r <= alu_z; n_r <= alu_n; c_r <= alu_c; v_r <= alu_v;
        end
    end

    // ===== special writes =====
    always_comb begin
        pe_ch      = 2'd0;
        pe_mode_we = 1'b0; pe_mode_in = 3'h0;
        pe_idxh_we = 1'b0; pe_idxh_in = 3'h0;
        pe_idx_we  = 1'b0; pe_idx_in  = 16'h0;
        pe_step_we = 1'b0; pe_step_in = 16'h0;
        pe_access  = 1'b0;
        gc_page_we = 1'b0; gc_page_in = 8'h0;
        gc_flush_en= 1'b0; gc_flush_ch = 2'd0;
        gc_access  = 1'b0; gc_we = 1'b0; gc_wdata = 16'h0;
        pb_read    = 1'b0;

        // GPU_START: configure CH0 from gpu_addr
        if (gpu_start) begin
            pe_ch      = 2'd0;
            pe_mode_we = 1'b1; pe_mode_in = 3'd4;
            pe_step_we = 1'b1; pe_step_in = 16'h0010;
            pe_idx_we  = 1'b1; pe_idx_in  = {1'b0, gpu_addr[15:1]};
            pe_idxh_we = 1'b1; pe_idxh_in = 3'h0;
            gc_page_we = 1'b1; gc_page_in = gpu_addr[23:16];
        end else if (run && cond_ok && is_mov && dst_code >= 6'd44) begin
            pe_ch = dst_code[1:0];
            if (dst_code < 6'd48) begin
                gc_access = 1'b1; gc_we = 1'b1; gc_wdata = alu_res;
            end else if (dst_code < 6'd52) begin
                pe_idx_we = 1'b1; pe_idx_in = alu_res;
            end else if (dst_code < 6'd56) begin
                pe_idxh_we = 1'b1; pe_idxh_in = alu_res[2:0];
            end else if (dst_code < 6'd60) begin
                pe_step_we = 1'b1; pe_step_in = alu_res;
            end else begin
                pe_mode_we = 1'b1; pe_mode_in = alu_res[10:8];
                gc_page_we = 1'b1; gc_page_in = alu_res[7:0];
            end
        end

        if (run && cond_ok && is_data_read) begin
            pe_ch     = src[1:0];
            gc_access = 1'b1;
            gc_we     = 1'b0;
            pe_access = 1'b1;
            pb_read   = 1'b1;
        end

        if (run && cond_ok && is_flush) begin
            gc_flush_en = 1'b1;
            gc_flush_ch = imm12_ctrl[1:0];
        end
    end

endmodule
