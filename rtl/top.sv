`timescale 1ns/1ps
`default_nettype none

// top: LX-GPU core.
//
// Naming:
//   *_i / *_o        ports of top
//   u_<blk>          instance of block <blk>; wires it drives are <blk>_<port>
//   ins_*            fields decoded from the instruction word (rom_fetch_data)
//   flag_*_r         Z N C V registers
//   ex_*             execute control
//   t_*              other signals produced by top; t_<blk>_<port> drives <blk>.<port>
//   *_r              registers
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
//   MOV ROMA, R   -> set address
//   MOV ROML, R   -> low 16 bits (no increment)
//   MOV ROMH, R   -> high 16 bits, write the word, ROMA++
//   MOV Rd, ROML  -> read low 16 (no increment)
//   MOV Rd, ROMH  -> read high 16, ROMA++
//
// ex_ack: an instruction completes when ex_ack = 1:
//   DATA read  -> blend_ack
//   ROM read   -> t_rom_wait_r (1 cycle, BRAM access port)
//   DATA write -> gc_access_done
//   others     -> 1
module top (
    input  logic        clk,
    input  logic        rst,

    // host interface
    input  logic        start_i,        // run from PC = 0 (no channel preload)
    input  logic        gpu_start_i,    // host START: preload stream channel, run
    input  logic        gpu_reset_i,    // host RESET: stop and clear
    input  logic [23:0] gpu_addr_i,     // byte address of the first packet
    output logic        halted_o,
    output logic        error_o,
    output logic [11:0] error_code_o,
    output logic [11:0] pc_o,

    // Wishbone master
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack_i
);

    // ================================================================
    // Declarations
    // ================================================================

    // ---- microcode ROM ----
    logic [31:0] rom_fetch_data;
    logic [31:0] rom_acc_rdata;
    logic        t_rom_acc_we, t_rom_acc_re;
    logic [31:0] t_rom_acc_wdata;
    logic [11:0] t_roma_r;
    logic [15:0] t_roml_r;
    logic        t_rom_wait_r;

    // ---- instruction fields ----
    logic        ins_l;
    logic [1:0]  ins_class;      // {J, I}: 00 reg-reg, 11 reg-imm, 01 LDI, 10 control
    logic [5:0]  ins_dst;
    logic        ins_write;
    logic [3:0]  ins_cond;
    logic        ins_f;
    logic [4:0]  ins_alu_op;
    logic [3:0]  ins_ctrl_op;
    logic        ins_r;          // control class: JMP/CALL target from a register
    logic [5:0]  ins_src;
    logic        ins_sx;
    logic [10:0] ins_imm11;
    logic [11:0] ins_addr;
    logic [15:0] ins_imm16;
    logic        ins_is_ldi, ins_is_ctrl, ins_is_mov, ins_is_flush;
    logic        ins_is_data_rd, ins_is_rom_rd, ins_is_data_wr;

    // ---- flags, condition, execute control ----
    logic        flag_z_r, flag_n_r, flag_c_r, flag_v_r;
    logic        cond_ok;
    logic        ex_ack;          // instruction can complete this cycle
    logic        ex_complete;     // ex_ack and the core is running and not skipped
    logic        ex_issue;        // same without ex_ack (launches port accesses)
    logic        ex_do;           // ex_complete and COND true
    logic        ex_issue_do;     // ex_issue and COND true

    // ---- register file ----
    logic [4:0]  t_rf_opa_addr, t_rf_opb_addr, t_rf_wr_addr;
    logic        t_rf_wr_en;
    logic [15:0] t_rf_wr_data;
    logic [15:0] rf_opa_data, rf_opb_data;

    // ---- ALU ----
    logic [15:0] t_alu_b;
    logic [15:0] alu_result, alu_mul_hi;
    logic        alu_flag_z, alu_flag_n, alu_flag_c, alu_flag_v, alu_upd_cv;
    logic [15:0] t_mhi_r;

    // ---- sequencer ----
    logic        t_seq_start;
    logic [11:0] t_seq_reg_target;
    logic        t_seq_irq_ack;
    logic        t_seq_loop_cnt_we, t_seq_loop_tgt_we, t_seq_shadow_we;
    logic [15:0] t_seq_loop_cnt, t_seq_shadow;
    logic [11:0] t_seq_loop_tgt;
    logic [11:0] seq_pc;
    logic [15:0] seq_shadow, seq_loop_cnt;
    logic [11:0] seq_loop_tgt;
    logic        seq_halted, seq_error;
    logic [11:0] seq_error_code;
    logic        seq_irq_pending;
    logic [7:0]  seq_irq_code;
    logic        seq_skip, seq_commit;

    // ---- pixel engine (channel pointers) ----
    logic [1:0]  t_ch;            // selected channel, shared by pe and gc
    logic        t_pe_mode_we, t_pe_idxh_we, t_pe_idx_we, t_pe_step_we;
    logic [2:0]  t_pe_mode, t_pe_idxh;
    logic [15:0] t_pe_idx, t_pe_step;
    logic        t_pe_access;
    logic [14:0] pe_word;
    logic [3:0]  pe_pos;
    logic [2:0]  pe_mode;
    logic        pe_prefetch;

    // ---- cache ----
    logic        t_gc_access, t_gc_we;
    logic [15:0] t_gc_wdata;
    logic        t_gc_page_we;
    logic [7:0]  t_gc_page;
    logic        t_gc_flush_en;
    logic [1:0]  t_gc_flush_ch;
    logic        gc_hit, gc_access_done, gc_flush_done, gc_error;
    logic [15:0] gc_rdata;
    logic [7:0]  gc_page;
    logic [23:0] gc_addr24;
    logic        gc_wb_cyc, gc_wb_stb, gc_wb_we;
    logic [23:0] gc_wb_adr;
    logic [15:0] gc_wb_dat;

    // ---- pixel blender ----
    logic        t_blend_read;
    logic [15:0] t_pixel_r;
    logic [15:0] blend_pixel, blend_word;
    logic        blend_ack, blend_busy;

    // ---- special-operand read ----
    logic [15:0] t_sp_rdata;

    // ================================================================
    // Instruction decode
    // NOTE: field positions below are the OLD layout (DST [28:23], WRITE 22,
    // COND [21:18], F 17). The accepted layout is COND [28:25], WRITE 24,
    // F 23, DST [22:17]. Update this block together with the assembler.
    // ================================================================
    assign ins_l       = rom_fetch_data[31];
    assign ins_class   = {rom_fetch_data[30], rom_fetch_data[29]};
    assign ins_cond    = rom_fetch_data[28:25];
    assign ins_write   = rom_fetch_data[24];
    assign ins_f       = rom_fetch_data[23];
    assign ins_dst     = rom_fetch_data[22:17];
    assign ins_alu_op  = rom_fetch_data[16:12];
    assign ins_r       = rom_fetch_data[16];
    assign ins_ctrl_op = rom_fetch_data[15:12];
    assign ins_imm16   = rom_fetch_data[15:0];
    assign ins_src     = rom_fetch_data[11:6];
    assign ins_sx      = rom_fetch_data[11];
    assign ins_imm11   = rom_fetch_data[10:0];
    assign ins_addr    = rom_fetch_data[11:0];

    assign ins_is_ldi  = (ins_class == 2'b01);
    assign ins_is_ctrl = (ins_class == 2'b10);
    assign ins_is_mov  = (ins_alu_op == 5'd0) && !ins_is_ctrl && !ins_is_ldi;
    assign ins_is_flush = ins_is_ctrl && ({ins_r, ins_ctrl_op} == 5'd6);

    assign ins_is_data_rd = !ins_is_ctrl && (ins_src >= 6'd44 && ins_src < 6'd48);
    assign ins_is_rom_rd  = !ins_is_ctrl && (ins_src == 6'd38 || ins_src == 6'd39);
    assign ins_is_data_wr = ins_is_mov && (ins_dst >= 6'd44 && ins_dst < 6'd48);

    // ================================================================
    // Condition and execute control
    // ================================================================
    always_comb begin
        case (ins_cond)
            4'd0:    cond_ok = 1'b1;
            4'd1:    cond_ok = flag_z_r;
            4'd2:    cond_ok = ~flag_z_r;
            4'd3:    cond_ok = flag_c_r;
            4'd4:    cond_ok = ~flag_c_r;
            4'd5:    cond_ok = flag_n_r;
            4'd6:    cond_ok = ~flag_n_r;
            4'd7:    cond_ok = flag_v_r;
            4'd8:    cond_ok = ~flag_v_r;
            4'd9:    cond_ok = flag_c_r & ~flag_z_r;
            4'd10:   cond_ok = ~flag_c_r | flag_z_r;
            4'd11:   cond_ok = (flag_n_r == flag_v_r);
            4'd12:   cond_ok = (flag_n_r != flag_v_r);
            4'd13:   cond_ok = ~flag_z_r & (flag_n_r == flag_v_r);
            4'd14:   cond_ok = flag_z_r | (flag_n_r != flag_v_r);
            default: cond_ok = 1'b0;
        endcase
    end

    assign ex_complete = ex_ack && !seq_skip && !seq_halted && !seq_error;
    // ex_issue does not depend on ex_ack: it breaks the loop
    // ex_ack <-> ex_complete <-> gc_access_done
    assign ex_issue    = !seq_skip && !seq_halted && !seq_error;
    assign ex_do       = ex_complete && cond_ok;
    assign ex_issue_do = ex_issue && cond_ok;

    assign ex_ack = ins_is_data_rd ? blend_ack
                  : ins_is_rom_rd  ? t_rom_wait_r
                  : ins_is_data_wr ? gc_access_done
                  : 1'b1;

    // ================================================================
    // Blocks
    // ================================================================
    microcode_rom #(.DEPTH(4096), .ADDR_W(12), .DATA_W(32),
                    .INIT_FILE("rtl/bootloader.hex")) u_rom (
        .clk(clk),
        .fetch_en_i(ex_ack),
        .fetch_addr_i(seq_pc),
        .fetch_data_o(rom_fetch_data),
        .acc_we_i(t_rom_acc_we),
        .acc_re_i(t_rom_acc_re),
        .acc_addr_i(t_roma_r),
        .acc_wdata_i(t_rom_acc_wdata),
        .acc_rdata_o(rom_acc_rdata)
    );

    regfile u_rf (
        .clk(clk),
        .opa_addr_i(t_rf_opa_addr), .opa_data_o(rf_opa_data),
        .opb_addr_i(t_rf_opb_addr), .opb_data_o(rf_opb_data),
        .wr_en_i(t_rf_wr_en), .wr_addr_i(t_rf_wr_addr), .wr_data_i(t_rf_wr_data)
    );

    alu u_alu (
        .alu_op(ins_alu_op), .a(rf_opa_data), .b(t_alu_b), .carry_in(flag_c_r),
        .result(alu_result), .mul_hi(alu_mul_hi),
        .flag_z(alu_flag_z), .flag_n(alu_flag_n),
        .flag_c(alu_flag_c), .flag_v(alu_flag_v),
        .upd_cv(alu_upd_cv)
    );

    assign t_seq_start   = start_i | gpu_start_i;
    assign t_seq_irq_ack = 1'b0;

    sequencer u_seq (
        .clk(clk), .rst(rst),
        .start_i(t_seq_start),
        .reset_halt_i(gpu_reset_i),
        .insn_class_i(ins_class), .ctrl_op_i(ins_ctrl_op), .cond_i(ins_cond),
        .loop_i(ins_l), .addr_i(ins_addr), .use_reg_i(ins_r),
        .reg_target_i(t_seq_reg_target),
        .flag_z_i(flag_z_r), .flag_n_i(flag_n_r),
        .flag_c_i(flag_c_r), .flag_v_i(flag_v_r),
        .insn_valid_i(ex_ack),
        .skip_o(seq_skip),
        .commit_o(seq_commit),
        .pc_o(seq_pc), .shadow_o(seq_shadow),
        .loop_cnt_o(seq_loop_cnt), .loop_tgt_o(seq_loop_tgt),
        .halted_o(seq_halted), .error_o(seq_error), .error_code_o(seq_error_code),
        .irq_pending_o(seq_irq_pending), .irq_code_o(seq_irq_code),
        .irq_ack_i(t_seq_irq_ack),
        .loop_cnt_we_i(t_seq_loop_cnt_we), .loop_cnt_i(t_seq_loop_cnt),
        .loop_tgt_we_i(t_seq_loop_tgt_we), .loop_tgt_i(t_seq_loop_tgt),
        .shadow_we_i(t_seq_shadow_we), .shadow_i(t_seq_shadow)
    );

    pixel_engine u_pe (
        .clk(clk), .rst(rst),
        .ch_i(t_ch),
        .mode_we_i(t_pe_mode_we), .mode_i(t_pe_mode),
        .idxh_we_i(t_pe_idxh_we), .idxh_i(t_pe_idxh),
        .idx_we_i(t_pe_idx_we),   .idx_i(t_pe_idx),
        .step_we_i(t_pe_step_we), .step_i(t_pe_step),
        .access_i(t_pe_access),
        .access_done_i(blend_ack),
        .word_o(pe_word), .pos_o(pe_pos), .mode_o(pe_mode),
        .prefetch_o(pe_prefetch)
    );

    gpu_cache u_gc (
        .clk(clk), .rst(rst),
        .ch_i(t_ch),
        .word_i(pe_word),
        .access_i(t_gc_access),
        .prefetch_i(pe_prefetch),
        .we_i(t_gc_we), .wdata_i(t_gc_wdata),
        .hit_o(gc_hit), .rdata_o(gc_rdata), .access_done_o(gc_access_done),
        .page_we_i(t_gc_page_we), .page_i(t_gc_page), .page_o(gc_page),
        .flush_en_i(t_gc_flush_en), .flush_ch_i(t_gc_flush_ch),
        .flush_done_o(gc_flush_done),
        .wb_cyc_o(gc_wb_cyc), .wb_stb_o(gc_wb_stb), .wb_we_o(gc_wb_we),
        .wb_adr_o(gc_wb_adr), .wb_dat_o(gc_wb_dat),
        .wb_ack_i(wb_ack_i), .wb_dat_i(wb_dat_i),
        .addr24_o(gc_addr24), .error_o(gc_error)
    );

    pixel_blender u_blend (
        .clk(clk), .rst(rst),
        .start_i(gc_access_done),
        .is_read_i(t_blend_read), .mode_i(pe_mode), .pos_i(pe_pos),
        .word_i(gc_rdata), .pixel_i(t_pixel_r),
        .pixel_o(blend_pixel), .word_o(blend_word),
        .ack_o(blend_ack), .busy_o(blend_busy)
    );

    // pixel to be written, captured when the write access starts
    always_ff @(posedge clk) begin
        if (rst)                         t_pixel_r <= 16'h0;
        else if (t_gc_access && t_gc_we) t_pixel_r <= alu_result;
    end

    // ================================================================
    // Outputs
    // ================================================================
    assign halted_o     = seq_halted;
    assign error_o      = seq_error;
    assign error_code_o = seq_error_code;
    assign pc_o         = seq_pc;

    assign wb_cyc_o = gc_wb_cyc;
    assign wb_stb_o = gc_wb_stb;
    assign wb_we_o  = gc_wb_we;
    assign wb_adr_o = gc_wb_adr;
    assign wb_dat_o = gc_wb_dat;

    // ================================================================
    // Special-operand read mux
    // ================================================================
    always_comb begin
        t_sp_rdata = 16'h0;
        case (ins_src)
            6'd32: t_sp_rdata = 16'h0;
            6'd33: t_sp_rdata = {12'h0, flag_v_r, flag_c_r, flag_n_r, flag_z_r};
            6'd34: t_sp_rdata = {12'h0, gc_wb_cyc, gc_flush_done,
                                 gc_access_done, gc_hit};
            6'd35: t_sp_rdata = seq_shadow;
            6'd36: t_sp_rdata = seq_loop_cnt;
            6'd37: t_sp_rdata = {4'h0, seq_loop_tgt};
            6'd38: t_sp_rdata = rom_acc_rdata[15:0];
            6'd39: t_sp_rdata = rom_acc_rdata[31:16];
            6'd40: t_sp_rdata = 16'h0;
            6'd41: t_sp_rdata = t_mhi_r;
            6'd44, 6'd45, 6'd46, 6'd47: t_sp_rdata = blend_pixel;
            default: t_sp_rdata = 16'h0;
        endcase
    end

    // ================================================================
    // Operands and ALU inputs
    // ================================================================
    assign t_rf_opa_addr = ins_dst[4:0];
    assign t_rf_opb_addr = (ins_class == 2'b00 ||
                            (ins_is_ctrl && ins_r && ins_src < 6'd32))
                           ? ins_src[4:0] : 5'd0;

    assign t_alu_b = (ins_class == 2'b11)
                   ? (ins_sx && ins_imm11[10] ? {5'h1F, ins_imm11}
                                              : {5'h00, ins_imm11})
                   : (ins_src < 6'd32 ? rf_opb_data : t_sp_rdata);

    assign t_seq_reg_target = (ins_src < 6'd32) ? rf_opb_data[11:0]
                                                : t_sp_rdata[11:0];

    always_ff @(posedge clk) begin
        if (rst)                                         t_mhi_r <= 16'h0;
        else if (ex_do && (ins_alu_op == 5'b11000))      t_mhi_r <= alu_mul_hi;
    end

    // ================================================================
    // ROM access (ROML / ROMH / ROMA)
    // ================================================================
    always_comb begin
        t_rom_acc_wdata = {16'h0, t_roml_r};
        if (ex_do && ins_is_mov && (ins_dst == 6'd39))
            t_rom_acc_wdata[31:16] = alu_result;
    end

    assign t_rom_acc_we = ex_do && ins_is_mov && (ins_dst == 6'd39);
    assign t_rom_acc_re = ins_is_rom_rd && ex_complete;

    always_ff @(posedge clk) begin
        if (rst) begin
            t_roma_r <= 12'h0;
            t_roml_r <= 16'h0;
        end else begin
            if (ex_do && ins_is_mov && (ins_dst == 6'd40))
                t_roma_r <= alu_result[11:0];
            if (ex_do && ins_is_mov && (ins_dst == 6'd38))
                t_roml_r <= alu_result;
            if (ex_do && ins_is_mov && (ins_dst == 6'd39))
                t_roma_r <= t_roma_r + 12'd1;
            if (ex_do && ins_is_rom_rd && (ins_src == 6'd39))
                t_roma_r <= t_roma_r + 12'd1;
        end
    end

    always_ff @(posedge clk) begin
        if (rst) t_rom_wait_r <= 1'b0;
        else     t_rom_wait_r <= ins_is_rom_rd && ex_complete;
    end

    // ================================================================
    // Register-file writeback
    // ================================================================
    assign t_rf_wr_en   = ex_do && ins_write && (ins_dst < 6'd32);
    assign t_rf_wr_addr = ins_dst[4:0];
    assign t_rf_wr_data = ins_is_ldi     ? ins_imm16
                        : ins_is_data_rd ? blend_pixel
                        : ins_is_rom_rd  ? t_sp_rdata
                        : alu_result;

    // ================================================================
    // System-register writes (SHADOW, LOOP_CNT, LOOP_TGT)
    // ================================================================
    logic t_sys_we;
    assign t_sys_we = ex_do && ins_write && !ins_is_ctrl
                      && ((ins_dst >= 6'd33 && ins_dst <= 6'd37)
                       || (ins_dst >= 6'd44));

    assign t_seq_shadow_we    = t_sys_we && (ins_dst == 6'd35);
    assign t_seq_shadow       = alu_result;
    assign t_seq_loop_cnt_we  = t_sys_we && (ins_dst == 6'd36);
    assign t_seq_loop_cnt     = alu_result;
    assign t_seq_loop_tgt_we  = t_sys_we && (ins_dst == 6'd37);
    assign t_seq_loop_tgt     = alu_result[11:0];

    // ================================================================
    // Flags
    // ================================================================
      always_ff @(posedge clk) begin
        if (rst) begin
            flag_z_r <= 1'b0; flag_n_r <= 1'b0;
            flag_c_r <= 1'b0; flag_v_r <= 1'b0;
        end else if (ex_do && ins_write && !ins_is_ctrl && (ins_dst == 6'd33)) begin
            {flag_v_r, flag_c_r, flag_n_r, flag_z_r} <= alu_result[3:0];
        end else if (ex_complete && ins_f && !ins_is_ctrl && !ins_is_ldi) begin
            flag_z_r <= alu_flag_z; flag_n_r <= alu_flag_n;
            flag_c_r <= alu_flag_c; flag_v_r <= alu_flag_v;
        end else if (ex_do && ins_f && !ins_is_ctrl && !ins_is_ldi) begin
            flag_z_r <= alu_flag_z;
            flag_n_r <= alu_flag_n;
            if (alu_upd_cv) begin
                flag_c_r <= alu_flag_c;
                flag_v_r <= alu_flag_v;
            end            
        end
    end

    // ================================================================
    // Channel / cache / blender control
    // ================================================================
    always_comb begin
        t_ch          = 2'd0;
        t_pe_mode_we  = 1'b0; t_pe_mode  = 3'h0;
        t_pe_idxh_we  = 1'b0; t_pe_idxh  = 3'h0;
        t_pe_idx_we   = 1'b0; t_pe_idx   = 16'h0;
        t_pe_step_we  = 1'b0; t_pe_step  = 16'h0;
        t_pe_access   = 1'b0;
        t_gc_page_we  = 1'b0; t_gc_page  = 8'h0;
        t_gc_flush_en = 1'b0; t_gc_flush_ch = 2'd0;
        t_gc_access   = 1'b0; t_gc_we    = 1'b0; t_gc_wdata = 16'h0;
        t_blend_read  = 1'b0;

        // host START: configure channel 0 as a 16 bpp word stream
        if (gpu_start_i) begin
            t_ch         = 2'd0;
            t_pe_mode_we = 1'b1; t_pe_mode = 3'd4;
            t_pe_step_we = 1'b1; t_pe_step = 16'h0010;
            t_pe_idx_we  = 1'b1; t_pe_idx  = {1'b0, gpu_addr_i[15:1]};
            t_pe_idxh_we = 1'b1; t_pe_idxh = 3'h0;
            t_gc_page_we = 1'b1; t_gc_page = gpu_addr_i[23:16];
        end else if (ex_issue_do && ins_is_mov && ins_dst >= 6'd44) begin
            t_ch = ins_dst[1:0];
            if (ins_dst < 6'd48) begin                 // DATA write
                t_gc_access = 1'b1; t_gc_we = 1'b1; t_gc_wdata = alu_result;
            end else if (ins_dst < 6'd52) begin        // IDX
                t_pe_idx_we = 1'b1; t_pe_idx = alu_result;
            end else if (ins_dst < 6'd56) begin        // IDXH
                t_pe_idxh_we = 1'b1; t_pe_idxh = alu_result[2:0];
            end else if (ins_dst < 6'd60) begin        // STEP
                t_pe_step_we = 1'b1; t_pe_step = alu_result;
            end else begin                             // MODE_PAGE
                t_pe_mode_we = 1'b1; t_pe_mode = alu_result[10:8];
                t_gc_page_we = 1'b1; t_gc_page = alu_result[7:0];
            end
        end

        // DATA read
        if (ex_issue_do && ins_is_data_rd) begin
            t_ch         = ins_src[1:0];
            t_gc_access  = 1'b1;
            t_gc_we      = 1'b0;
            t_pe_access  = 1'b1;
            t_blend_read = 1'b1;
        end

        // FLUSH
        if (ex_issue_do && ins_is_flush) begin
            t_gc_flush_en = 1'b1;
            t_gc_flush_ch = ins_addr[1:0];
        end
    end

endmodule
