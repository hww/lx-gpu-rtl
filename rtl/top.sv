`timescale 1ns/1ps
`default_nettype none

// top: связка архитектурных узлов.
// Карта операндов (6 бит, 0..63):
//   0..31   — R0..R31 (регистры, R/W)
//   32..39  — системные:
//     32 ZERO (R=0, W=ignore)
//     33 FLAGS (R/W)
//     34 SHADOW (R/W)
//     35 LOOP.CNT (R/W)
//     36 LOOP.TGT (R/W)
//     37 BUSY (R only)
//     38 MHI (R only)
//     39 reserved
//   40..43  — DATA[0..3] (R/W)
//   44..47  — reserved
//   48..63  — каналы 4×4 (только W):
//     48 + 4*ch + 0: IDX      (W)
//     48 + 4*ch + 1: IDXH     (W)
//     48 + 4*ch + 2: STEP     (W)
//     48 + 4*ch + 3: MODE_PAGE (W, {PAGE[7:0], MODE[7:0]})
//   FLUSH — CTRL class, reg_op=7, imm12[1:0] = channel.
//   Спецоперанд-приёмник только в MOV (C3a).
module top (
    input  logic        clk,
    input  logic        rst,
    output logic        halted,
    output logic        error,
    input  logic        start,
    input  logic        rom_we,
    input  logic [11:0] rom_addr,
    input  logic [31:0] rom_wdata,

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
    microcode_rom #(.DEPTH(4096), .ADDR_W(12), .DATA_W(32), .INIT_FILE("rtl/bootloader.hex")) rom (
        .clk(clk), .addr(pc), .data(insn),
        .we(rom_we), .waddr(rom_addr), .wdata(rom_wdata), .ack(ack), .rst(rst)
    );

    // ===== decode =====
    logic        l;
    logic [1:0]  imm_class;
    logic [4:0]  dst;
    logic        ds;
    logic [3:0]  cond;
    logic        f;
    logic [4:0]  reg_op;
    logic [5:0]  src;
    logic [11:0] imm12;
    logic        sx;

    assign l         = insn[31];
    assign imm_class = {insn[30], insn[29]};
    assign dst       = insn[28:24];
    assign ds        = insn[23];
    assign cond      = insn[22:19];
    assign f         = insn[18];
    assign reg_op    = insn[17:13];
    assign src       = insn[12:7];
    assign imm12     = insn[11:0];
    assign sx        = insn[12];

    // ===== flags =====
    logic z_r, n_r, c_r, v_r;
    logic z, n, c, v;
    assign z = z_r; assign n = n_r; assign c = c_r; assign v = v_r;

    // ===== register file =====
    logic [4:0]  ra, rb, wa;
    logic [15:0] rdata_a, rdata_b, wdata;
    logic        we;
    regfile rf (
        .clk(clk), .rst(rst),
        .ra(ra), .rdata_a(rdata_a),
        .rb(rb), .rdata_b(rdata_b),
        .we(we), .wa(wa), .wdata(wdata)
    );

    // ===== ALU =====
    logic [4:0]  alu_op;
    logic [15:0] alu_a, alu_b, alu_res, alu_mhi;
    logic        alu_nowrite;
    logic        alu_z, alu_n, alu_c, alu_v;
    alu al (
        .clk(clk), .rst(rst),
        .op(alu_op), .a(alu_a), .b(alu_b), .c_in(c_r),
        .res(alu_res), .mhi(alu_mhi), .nowrite(alu_nowrite),
        .z(alu_z), .n(alu_n), .c(alu_c), .v(alu_v)
    );
    // ===== ack (объявляем до инстанса sequencer) =====
    logic ack;

    // ===== sequencer =====
    logic [11:0] pc_next, error_code;
    logic [15:0] shadow;
    logic [15:0] loop_cnt;
    logic [11:0] loop_tgt;
    logic        irq_pending;
    logic [11:0] irq_code;
    logic [11:0] jmpr_target;
    logic        stall;
    sequencer seq (
        .clk(clk), .rst(rst), .start(start),
        .imm_class(imm_class), .reg_op(reg_op), .cond(cond),
        .l(l), .imm12(imm12),
        .z(z_r), .n(n_r), .c(c_r), .v(v_r),
        .jmpr_target(jmpr_target),
        .pc(pc), .pc_next(pc_next), .shadow(shadow),
        .loop_cnt(loop_cnt), .loop_tgt(loop_tgt),
        .halted(halted), .error(error), .error_code(error_code),
        .irq_pending(irq_pending), .irq_code(irq_code),
        .ack(ack),
        .loop_cnt_we(1'b0), .loop_cnt_in(16'h0),
        .loop_tgt_we(1'b0), .loop_tgt_in(12'h0)
    );

    // ===== pixel_engine =====
    logic [1:0]  pe_ch;
    logic        pe_mode_we;
    logic [2:0]  pe_mode_in;
    logic        pe_idxh_we;
    logic [2:0]  pe_idxh_in;
    logic        pe_idx_we;
    logic [15:0] pe_idx_in;
    logic        pe_step_we;
    logic [15:0] pe_step_in;
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
        .idx_we(pe_idx_we), .idx_in(pe_idx_in),
        .step_we(pe_step_we), .step_in(pe_step_in),
        .access(pe_access),
        .access_done(pb_ack),          // idx инкрементируется по ack блендера
        .word_out(pe_word_out),
        .pos(pe_pos), .mode(pe_mode),
        .prefetch_req(pe_prefetch_req)
    );



    // ===== gpu_cache =====
    logic        gc_page_we;
    logic [7:0]  gc_page_in;
    logic [7:0]  gc_page_out;
    logic        gc_flush_en;
    logic [1:0]  gc_flush_ch;
    logic        gc_flush_done;
    logic        gc_access;
    logic        gc_we;
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
    logic        pb_start;
    logic        pb_read;
    logic [2:0]  pb_mode;
    logic [3:0]  pb_pos;
    logic [15:0] pb_word_in, pb_pixel_in;
    logic [15:0] pb_pixel_out, pb_word_out;
    logic        pb_ack;
    logic        pb_busy;
    pixel_blender pb (
        .clk(clk), .rst(rst),
        .start(pb_start),
        .read(pb_read), .mode(pb_mode), .pos(pb_pos),
        .word_in(pb_word_in), .pixel_in(pb_pixel_in),
        .pixel_out(pb_pixel_out), .word_out(pb_word_out),
        .ack(pb_ack), .busy(pb_busy)
    );

    assign pb_mode     = pe_mode;
    assign pb_pos      = pe_pos;          // текущая позиция (не next)
    assign pb_word_in  = gc_rdata;        // данные из кэша
    assign pb_pixel_in = alu_res;

    // start от кэша: данные валидны
    assign pb_start    = gc_access_done;

    // ===== Wishbone =====
    assign wb_cyc   = gc_wb_cyc;
    assign wb_stb   = gc_wb_stb;
    assign wb_we    = gc_wb_we;
    assign wb_adr   = gc_wb_adr;
    assign wb_dat_o = gc_wb_dat_o;

    // ===== operand codes =====
    logic [5:0]  dst_code, src_code;
    assign dst_code = {ds, dst};
    assign src_code = src;

    logic is_ctrl;
    assign is_ctrl = (imm_class == 2'b10);

    // ===== operand A (always from register file) =====
    assign ra = dst[4:0];
    assign alu_a = rdata_a;

    // ===== special read =====
    logic [15:0] sp_rd;

    always_comb begin
        sp_rd = 16'h0;

        if (src_code >= 6'd32 && src_code < 6'd40) begin
            // системные 32..39
            case (src_code)
                6'd32: sp_rd = 16'h0;                              // ZERO
                6'd33: sp_rd = {12'h0, v_r, c_r, n_r, z_r};        // FLAGS
                6'd34: sp_rd = shadow;                             // SHADOW
                6'd35: sp_rd = loop_cnt;                           // LOOP.CNT
                6'd36: sp_rd = {4'h0, loop_tgt};                   // LOOP.TGT
                6'd37: sp_rd = {12'h0, gc_wb_cyc, gc_flush_done, gc_access_done, gc_hit};  // BUSY
                6'd38: sp_rd = alu_mhi;                            // MHI
                6'd39: sp_rd = 16'h0;                              // reserved
                default: sp_rd = 16'h0;
            endcase
        end else if (src_code >= 6'd40 && src_code < 6'd44) begin
            // DATA[0..3] — read pixel
            sp_rd = pb_pixel_out;
        end else begin
            // 48..63 — каналы (только W). Чтение = 0.
            sp_rd = 16'h0;
        end
    end

    // ===== operand B =====
    assign rb = (imm_class == 2'b00 && src_code < 6'd32) ? src_code[4:0] : 5'd0;
    assign alu_b = (imm_class == 2'b11) ?
                   (sx && imm12[11] ? {4'hF, imm12} : {4'h0, imm12}) :
                   (src_code < 6'd32 ? rdata_b : sp_rd);

    // ===== ALU op =====
    assign alu_op = reg_op;

    // ===== writeback (R0..R31) =====
    // Протокол: ack — однотактовый строп от каждого устройства.
    // top.sv собирает AND всех ack. we — импульс по ack.
    logic is_data_read;
    assign is_data_read = !is_ctrl && (src_code >= 6'd40 && src_code < 6'd44);

    // ack: для DATA-операций — от блендера (pb_ack), иначе — сразу.
    assign ack = is_data_read ? pb_ack : 1'b1;

    // we — импульс по ack, только для MOV в R0..R31
    assign we    = ack && !is_ctrl && !is_flush && !alu_nowrite && (dst_code < 6'd32);
    assign wa    = dst[4:0];
    assign wdata = is_data_read ? pb_pixel_out : alu_res;   // для DATA — из блендера


    // ===== special write (dst_code >= 32, only MOV) =====
    logic is_mov;
    assign is_mov = (reg_op[4:3] == 2'b00 && reg_op[2:1] == 2'b00 && reg_op[0] == 1'b0);

    logic is_flush;
    assign is_flush = is_ctrl && (reg_op == 5'd7);

    always_comb begin
        pe_ch = 2'd0;
        pe_mode_we = 1'b0; pe_mode_in = 3'h0;
        pe_idxh_we = 1'b0; pe_idxh_in = 3'h0;
        pe_idx_we  = 1'b0; pe_idx_in  = 16'h0;
        pe_step_we = 1'b0; pe_step_in = 16'h0;
        pe_access  = 1'b0;
        gc_page_we = 1'b0; gc_page_in = 8'h0;
        gc_flush_en = 1'b0; gc_flush_ch = 2'd0;
        gc_access = 1'b0; gc_we = 1'b0; gc_wdata = 16'h0;
        pb_read = 1'b0;

        // --- special write (only MOV, dst_code >= 32) ---
        if (is_mov && dst_code >= 6'd32) begin
            if (dst_code < 6'd40) begin
                // системные 32..39: только FLAGS, SHADOW, LOOP.CNT, LOOP.TGT (R/W)
                // ZERO/BUSY/MHI — игнор
            end else if (dst_code < 6'd44) begin
                // DATA[0..3] (40..43): write
                pe_ch = dst_code[1:0];
                gc_access = 1'b1;
                gc_we = 1'b1;
                gc_wdata = alu_res;
            end else if (dst_code >= 6'd48) begin
                // каналы (48..63): 4 вида
                pe_ch = dst_code[3:2];
                case (dst_code[1:0])
                    2'd0: begin pe_idx_we  = 1'b1; pe_idx_in  = alu_res[15:0]; end
                    2'd1: begin pe_idxh_we = 1'b1; pe_idxh_in = alu_res[2:0];  end
                    2'd2: begin pe_step_we = 1'b1; pe_step_in = alu_res[15:0]; end
                    2'd3: begin
                        pe_mode_we = 1'b1; pe_mode_in = alu_res[2:0];
                        gc_page_we = 1'b1; gc_page_in = alu_res[15:8];
                    end
                endcase
            end
        end

        // --- DATA read (src_code 40..43) ---
        if (!is_ctrl && src_code >= 6'd40 && src_code < 6'd44) begin
            pe_ch = src_code[1:0];
            gc_access = 1'b1;
            gc_we = 1'b0;
            pe_access = 1'b1;
            pb_read = 1'b1;
        end

        // --- FLUSH: CTRL, reg_op=7, imm12[1:0] = channel ---
        if (is_flush) begin
            gc_flush_en = 1'b1;
            gc_flush_ch = imm12[1:0];
        end
    end

    // ===== ack для sequencer'а =====
    // stalled больше нет: pc шагает по ack, а не по !stall.

    // ===== JMPR target =====
    assign jmpr_target = alu_res[11:0];

endmodule