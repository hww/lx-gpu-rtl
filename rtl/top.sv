`timescale 1ns/1ps
`default_nettype none

// top: LX-GPU core.
//
// Naming:
//   *_i / *_o        ports of top
//   u_<blk>          instance of block <blk>; wires it drives are <blk>_<port>
//   ins_*            fields decoded from the held ROM fetch word
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
// ex_ack: the sequencer may commit only after the active device responds:
//   DATA read/write -> blend_ack (CPU <-> blender)
//   ROM read        -> registered ROM access-port response
//   FLUSH           -> gc_flush_done
//   other ops       -> 1
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
    input  logic        wb_ack_i,

    output logic dbg_blend_ack_o
);

    // ================================================================
    // Declarations
    // ================================================================

    // ---- microcode ROM ----
    logic [31:0] rom_fetch_data /*verilator public*/;
    logic [31:0] rom_acc_rdata;
    logic        t_rom_acc_we, t_rom_acc_re;
    logic [31:0] t_rom_acc_wdata;
    logic [11:0] t_roma_r;
    logic [15:0] t_roml_r;
    logic        rom_rd_pending_r, rom_rd_ready_r;
    logic        rom_rd_issue;

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
    logic        gc_hit, gc_ack, gc_flush_done, gc_error;
    logic [15:0] gc_rdata;
    logic [7:0]  gc_page;
    logic [23:0] gc_addr24;
    logic        gc_wb_cyc, gc_wb_stb, gc_wb_we;
    logic [23:0] gc_wb_adr;
    logic [15:0] gc_wb_dat;

    // ---- pixel blender (Wishbone slave) ----
    logic        blend_cyc, blend_stb, blend_we;
    logic [2:0]  blend_adr;
    logic        access_pending_r;   
    logic [15:0] blend_dat_o;
    logic        blend_ack;
    logic        blend_gc_ack_r;
    logic        pe_access_done;
    logic        blend_read_done_r;
    logic        blend_rmw_done_r;
    logic        blend_rmw_we_d;
    logic        blend_rmw_req, blend_rmw_we;
    logic [15:0] blend_rmw_dat;

    // ---- special-operand read ----
    logic [15:0] t_sp_rdata;

    // ================================================================
    // Instruction decode. ROM output is registered; TOP_DECODE provides
    // the synchronous register file with one full cycle to read operands.
    assign ins_word = (top_state_r == TOP_EXEC && seq_skip)
                    ? 32'h0000_0000 : rom_fetch_data;

    assign ins_l       = ins_word[31];
    assign ins_class   = {ins_word[30], ins_word[29]};
    assign ins_cond    = ins_word[28:25];
    assign ins_write   = ins_word[24];
    assign ins_f       = ins_word[23];
    assign ins_dst     = ins_word[22:17];
    assign ins_alu_op  = ins_word[16:12];
    assign ins_r       = ins_word[16];
    assign ins_ctrl_op = ins_word[15:12];
    assign ins_imm16   = ins_word[15:0];
    assign ins_src     = ins_word[11:6];
    assign ins_sx      = ins_word[11];
    assign ins_imm11   = ins_word[10:0];
    assign ins_addr    = ins_word[11:0];

    assign ins_is_ldi   = (ins_class == 2'b01);
    assign ins_is_ctrl  = (ins_class == 2'b10);
    assign ins_is_mov   = (ins_alu_op == 5'd0) && !ins_is_ctrl && !ins_is_ldi;
    assign ins_is_flush = ins_is_ctrl && ({ins_r, ins_ctrl_op} == 5'd6);
    assign ins_is_data_rd = !ins_is_ctrl &&
                            (ins_src >= 6'd44 && ins_src < 6'd48);
    assign ins_is_rom_rd  = !ins_is_ctrl &&
                            (ins_src == 6'd38 || ins_src == 6'd39);
    assign ins_is_data_wr = ins_is_mov &&
                            (ins_dst >= 6'd44 && ins_dst < 6'd48);

    // Condition evaluation.
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

    assign ex_issue    = (top_state_r == TOP_EXEC) &&
                         !seq_skip && !seq_halted && !seq_error;
    assign ex_issue_do = ex_issue && cond_ok;
    assign ex_ack      = !cond_ok ? 1'b1 :
                         ins_is_data_rd ? blend_ack :
                         ins_is_data_wr ? blend_ack :
                         ins_is_rom_rd  ? rom_rd_ready_r :
                         ins_is_flush   ? gc_flush_done : 1'b1;
    assign ex_complete = seq_commit;
    assign ex_do       = ex_complete && cond_ok;

    // One-shot synchronous read from the ROM access port.
    assign rom_rd_issue = ex_issue_do && ins_is_rom_rd &&
                          !rom_rd_pending_r && !rom_rd_ready_r;

    assign rom_fetch_en = (top_state_r == TOP_FETCH);

    // Fetch -> decode -> execute. PC changes only on a committed instruction.
    // A branch bubble is consumed without discarding the fetched target word.
    always_ff @(posedge clk) begin
        if (rst) begin
            top_state_r <= TOP_IDLE;
        end else if (gpu_reset_i) begin
            top_state_r <= TOP_IDLE;
        end else if (t_seq_start) begin
            top_state_r <= TOP_FETCH;
        end else begin
            case (top_state_r)
                TOP_IDLE: begin
                    // Wait for START.
                end
                TOP_FETCH: begin
                    top_state_r <= TOP_DECODE;
                end
                TOP_DECODE: begin
                    top_state_r <= TOP_EXEC;
                end
                TOP_EXEC: begin
                    if (seq_skip) begin
                        top_state_r <= TOP_DECODE;
                    end else if (seq_commit) begin
                        top_state_r <= TOP_FETCH;
                    end
                end
                default: begin
                    top_state_r <= TOP_IDLE;
                end
            endcase
        end
    end

    always_ff @(posedge clk) begin
        if (rst || gpu_reset_i || t_seq_start) begin
            rom_rd_pending_r <= 1'b0;
            rom_rd_ready_r   <= 1'b0;
        end else begin
            if (rom_rd_issue) begin
                rom_rd_pending_r <= 1'b1;
                rom_rd_ready_r   <= 1'b0;
            end else if (rom_rd_pending_r) begin
                rom_rd_pending_r <= 1'b0;
                rom_rd_ready_r   <= 1'b1;
            end
            if (ex_complete && ins_is_rom_rd)
                rom_rd_ready_r <= 1'b0;
        end
    end

    always_ff @(posedge clk) begin
        if (rst) blend_gc_ack_r <= 1'b0;
        else     blend_gc_ack_r <= gc_ack;
    end

    // ---- top-level instruction pipeline ----
    typedef enum logic [2:0] {
        TOP_IDLE   = 3'd0,
        TOP_FETCH  = 3'd1,
        TOP_DECODE = 3'd2,
        TOP_EXEC   = 3'd3
    } top_state_t;

    top_state_t top_state_r;
    logic [31:0] ins_word;
    logic        rom_fetch_en;

    // Blocks
    // ================================================================
    microcode_rom #(.DEPTH(4096), .ADDR_W(12), .DATA_W(32),
                    .INIT_FILE("rtl/bootloader.hex")) u_rom (
        .clk(clk),
        .fetch_en_i(rom_fetch_en),
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
        .insn_valid_i((top_state_r == TOP_EXEC) && ex_ack),
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
        .access_done_i(pe_access_done),
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
        .hit_o(gc_hit), .rdata_o(gc_rdata), .ack_o(gc_ack),
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
        .cyc_i(blend_cyc), .stb_i(blend_stb), .we_i(blend_we),
        .adr_i(blend_adr), .mode_i(pe_mode), .pos_i(pe_pos),
        .dat_i(gc_rdata), .pixel_i(alu_result),
        .dat_o(blend_dat_o), .ack_o(blend_ack),
        .ack_i(blend_gc_ack_r),
        .rmw_req_o(blend_rmw_req), .rmw_we_o(blend_rmw_we),
        .rmw_dat_o(blend_rmw_dat)
    );

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
                                 gc_ack, gc_hit};
            6'd35: t_sp_rdata = seq_shadow;
            6'd36: t_sp_rdata = seq_loop_cnt;
            6'd37: t_sp_rdata = {4'h0, seq_loop_tgt};
            6'd38: t_sp_rdata = rom_acc_rdata[15:0];
            6'd39: t_sp_rdata = rom_acc_rdata[31:16];
            6'd40: t_sp_rdata = 16'h0;
            6'd41: t_sp_rdata = t_mhi_r;
            6'd44, 6'd45, 6'd46, 6'd47: t_sp_rdata = blend_dat_o;
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
    always_comb begin
        t_rom_acc_wdata = {16'h0, t_roml_r};
        if (ex_do && ins_is_mov && (ins_dst == 6'd39))
            t_rom_acc_wdata[31:16] = alu_result;
    end

    assign t_rom_acc_we = ex_do && ins_is_mov && (ins_dst == 6'd39);
    assign t_rom_acc_re = rom_rd_issue;

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

    // Register-file writeback
    // ================================================================
    assign t_rf_wr_en   = ex_do && ins_write && (ins_dst < 6'd32);
    assign t_rf_wr_addr = ins_dst[4:0];
    assign t_rf_wr_data = ins_is_ldi     ? ins_imm16
                        : ins_is_data_rd ? blend_dat_o
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
    always_ff @(posedge clk) begin
        if (rst) begin
            flag_z_r <= 1'b0;
            flag_n_r <= 1'b0;
            flag_c_r <= 1'b0;
            flag_v_r <= 1'b0;
        end else if (ex_do && ins_write && !ins_is_ctrl &&
                     (ins_dst == 6'd33)) begin
            {flag_v_r, flag_c_r, flag_n_r, flag_z_r} <= alu_result[3:0];
        end else if (ex_do && ins_f && !ins_is_ctrl && !ins_is_ldi) begin
            flag_z_r <= alu_flag_z;
            flag_n_r <= alu_flag_n;
            if (alu_upd_cv) begin
                flag_c_r <= alu_flag_c;
                flag_v_r <= alu_flag_v;
            end
        end
    end

    // Channel / cache control.
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

        if (gpu_start_i) begin
            t_ch         = 2'd0;
            t_pe_mode_we = 1'b1; t_pe_mode = 3'd4;
            t_pe_step_we = 1'b1; t_pe_step = 16'h0010;
            t_pe_idx_we  = 1'b1; t_pe_idx  = {1'b0, gpu_addr_i[15:1]};
            t_pe_idxh_we = 1'b1; t_pe_idxh = 3'h0;
            t_gc_page_we = 1'b1; t_gc_page = gpu_addr_i[23:16];
        end else if (ex_issue_do && ins_is_mov && ins_dst >= 6'd44) begin
            t_ch = ins_dst[1:0];
            if (ins_dst < 6'd48) begin
                // DATA write is handled by pixel_blender's RMW engine.
            end else if (ins_dst < 6'd52) begin
                t_pe_idx_we = 1'b1; t_pe_idx = alu_result;
            end else if (ins_dst < 6'd56) begin
                t_pe_idxh_we = 1'b1; t_pe_idxh = alu_result[2:0];
            end else if (ins_dst < 6'd60) begin
                t_pe_step_we = 1'b1; t_pe_step = alu_result;
            end else begin
                t_pe_mode_we = 1'b1; t_pe_mode = alu_result[10:8];
                t_gc_page_we = 1'b1; t_gc_page = alu_result[7:0];
            end
        end

        if (blend_cyc && !blend_we && !blend_read_done_r) begin
            t_ch         = blend_adr[2:1];
            t_gc_access  = 1'b1;
            t_gc_we      = 1'b0;
            t_pe_access  = 1'b1;
        end

        if (ex_issue_do && ins_is_flush) begin
            t_gc_flush_en = 1'b1;
            t_gc_flush_ch = ins_addr[1:0];
        end

        if (blend_rmw_req && !blend_rmw_done_r) begin
            t_ch        = blend_adr[2:1];
            t_gc_access = 1'b1;
            t_gc_we     = blend_rmw_we;
            t_gc_wdata  = blend_rmw_dat;
        end
    end

    // Advance the pixel pointer once per completed CPU DATA operation,
    // not on the blender's internal RMW read and write acknowledgements.
    assign pe_access_done = ex_complete && (ins_is_data_rd || ins_is_data_wr);

    // Suppress repeated cache requests while the blender is still waiting
    // for the registered ACK to propagate back through its RMW state machine.
    always_ff @(posedge clk) begin
        if (rst) begin
            blend_read_done_r <= 1'b0;
            blend_rmw_done_r  <= 1'b0;
            blend_rmw_we_d    <= 1'b0;
        end else begin
            if (!blend_cyc)
                blend_read_done_r <= 1'b0;
            else if (gc_ack && !blend_we)
                blend_read_done_r <= 1'b1;

            if (!blend_rmw_req || (blend_rmw_we != blend_rmw_we_d))
                blend_rmw_done_r <= 1'b0;
            else if (gc_ack && blend_rmw_req)
                blend_rmw_done_r <= 1'b1;

            blend_rmw_we_d <= blend_rmw_we;
        end
    end

    // Blender request controller: keep request/address/data stable until ACK.
    always_ff @(posedge clk) begin
        if (rst) begin
            blend_cyc        <= 1'b0;
            blend_stb        <= 1'b0;
            blend_we         <= 1'b0;
            blend_adr        <= 3'd0;
            access_pending_r <= 1'b0;
        end else begin
            if (ex_issue_do && (ins_is_data_rd || ins_is_data_wr) &&
                !access_pending_r) begin
                blend_cyc        <= 1'b1;
                blend_stb        <= 1'b1;
                blend_we         <= ins_is_data_wr;
                blend_adr        <= ins_is_data_wr
                                  ? {ins_dst[1:0], 1'b1}
                                  : {ins_src[1:0], 1'b0};
                access_pending_r <= 1'b1;
            end

            if (blend_cyc && blend_ack) begin
                blend_cyc <= 1'b0;
                blend_stb <= 1'b0;
            end

            if (ex_complete && access_pending_r)
                access_pending_r <= 1'b0;
        end
    end

    assign dbg_blend_ack_o = blend_ack;

endmodule