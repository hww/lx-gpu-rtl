`timescale 1ns/1ps
`default_nettype none

// top: LX-GPU core. Three stages F -> D -> X, no state machine.
//
//   F  fetch_unit.pc_o -> ROM address; the ROM output register is the IR.
//   D  IR valid. Regfile read addresses come straight from the IR and the
//      regfile output registers capture the operands. fetch_unit resolves
//      JMP/CALL/RET and the L bit here.
//   X  x_ins_r + operands: ALU, flags, register / CSR / device writes,
//      devices, JMP by register, HALT, IRQ, FLUSH.
//
// Stall: X is busy while its device has not acknowledged (x_busy). Then adv = 0
// and the IR, PC, regfile outputs and x_ins_r all hold.
// One device per instruction: the assembler must reject an instruction whose
// source and destination are both devices (DATA, ROM).
//
// Naming: *_i / *_o ports; u_<blk> instances, wires they drive are <blk>_<port>;
// x_* fields of the X-stage word; t_* other signals made here; *_r registers.
//
// Operand map (6 bits):
//   0..31 R0..R31   32 ZERO   33 FLAGS   34 BUSY   35 SHADOW   36 LOOP_CNT
//   37 LOOP_TGT   38 ROML   39 ROMH   40 ROMA(W)   41 MHI   42..43 reserved
//   44..47 DATA[0..3]   48..51 IDX[0..3]   52..55 IDXH[0..3]
//   56..59 STEP[0..3]   60..63 MODEPAGE[0..3]
// Special operands as destination: MOV only (assembler rule).
module top (
    input  logic        clk,
    input  logic        rst,

    // host interface
    input  logic        start_i,        // run from PC = 0
    input  logic        gpu_start_i,    // host START: preload channel 0, run
    input  logic        gpu_reset_i,    // host RESET: stop and clear
    input  logic [23:0] gpu_addr_i,     // byte address of the first packet
    input  logic        irq_ack_i,      // host read IRQ_CODE
    output logic        halted_o,
    output logic        error_o,        // halted with a non-zero code
    output logic [6:0]  halt_code_o,
    output logic        irq_o,
    output logic [7:0]  irq_code_o,
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

    localparam logic [3:0] CTRL_JMP   = 4'd1;
    localparam logic [3:0] CTRL_HALT  = 4'd4;
    localparam logic [3:0] CTRL_IRQ   = 4'd5;
    localparam logic [3:0] CTRL_FLUSH = 4'd6;

    // ================================================================
    // Declarations
    // ================================================================
    // run control
    logic        halted_r;
    logic [6:0]  halt_code_r;
    logic        irq_pending_r;
    logic [7:0]  irq_code_r;
    logic        t_go, t_clear, t_halt_set;
    logic [6:0]  t_halt_code;

    // ROM
    logic [31:0] rom_fetch_data /* verilator public_flat_rw */;
    logic [31:0] rom_acc_rdata;
    logic        t_rom_acc_we, t_rom_acc_re;
    logic [31:0] t_rom_acc_wdata;
    logic [11:0] t_roma_r;
    logic [15:0] t_roml_r;
    logic        t_rom_ack_r, t_rom_req, t_romh_wr, t_rom_prot_viol;

    // fetch unit
    logic [11:0] fu_pc;
    logic        fu_d_valid, fu_hold;
    logic [15:0] fu_shadow, fu_loop_cnt;
    logic [11:0] fu_loop_tgt;
    logic [2:0]  t_fu_x_hazard;
    logic        t_fu_x_jump, t_fu_csr_we;
    logic [11:0] t_fu_x_target;
    logic [1:0]  t_fu_csr_sel;

    // pipeline control
    logic        adv;
    logic        x_valid_r;
    logic [31:0] x_ins_r;
    logic        x_kill;

    // X-stage word fields
    logic        x_is_rr, x_is_imm, x_is_ldi, x_is_ctrl;
    logic [3:0]  x_cond;
    logic        x_write, x_f, x_r, x_sx;
    logic [5:0]  x_dst, x_src;
    logic [4:0]  x_alu_op;
    logic [3:0]  x_ctrl_op;
    logic [10:0] x_imm11;
    logic [11:0] x_addr;
    logic [15:0] x_imm16;
    logic        x_is_mov, x_src_sp, x_sp_dst;
    logic        x_is_ch_rd, x_is_ch_wr, x_is_rom_rd, x_is_flush;

    // condition, flags, execute control
    logic        flag_z_r, flag_n_r, flag_c_r, flag_v_r;
    logic        x_cond_ok, x_exec, x_dev_req, x_dev_ack, x_busy, x_commit, x_do;

    // register file and ALU
    logic [4:0]  t_rf_opa_addr, t_rf_opb_addr;
    logic [15:0] rf_opa_data, rf_opb_data;
    logic        t_rf_wr_en;
    logic [15:0] t_alu_b, alu_result, alu_mul_hi;
    logic        alu_flag_z, alu_flag_n, alu_flag_c, alu_flag_v, alu_upd_cv;
    logic [15:0] t_mhi_r;
    logic [15:0] t_sp_rdata;

    // channel unit
    logic [1:0]  t_ch_sel;
    logic        t_ch_idx_we, t_ch_idxh_we, t_ch_step_we, t_ch_modepage_we;
    logic        t_ch_data_req, t_ch_data_we, t_ch_flush_req;
    logic        ch_data_ack, ch_flush_ack;
    logic        ch_error /* verilator public_flat_rw */;
    logic [15:0] ch_data_rdata;
    logic [3:0]  ch_status /* verilator public_flat_rw */;

    // ================================================================
    // X-stage word
    // ================================================================
    assign x_is_rr   = (x_ins_r[30:29] == 2'b00);
    assign x_is_ldi  = (x_ins_r[30:29] == 2'b01);
    assign x_is_ctrl = (x_ins_r[30:29] == 2'b10);
    assign x_is_imm  = (x_ins_r[30:29] == 2'b11);

    assign x_cond    = x_ins_r[28:25];
    assign x_write   = x_is_ldi | x_ins_r[24];      // LDI always writes
    assign x_f       = ~x_is_ldi & x_ins_r[23];     // LDI never sets flags
    assign x_dst     = x_ins_r[22:17];
    assign x_alu_op  = x_is_ldi ? 5'd0 : x_ins_r[16:12];   // LDI = MOV
    assign x_r       = x_ins_r[16];
    assign x_ctrl_op = x_ins_r[15:12];
    assign x_src     = x_ins_r[11:6];
    assign x_sx      = x_ins_r[11];
    assign x_imm11   = x_ins_r[10:0];
    assign x_addr    = x_ins_r[11:0];
    assign x_imm16   = x_ins_r[15:0];

    assign x_is_mov    = !x_is_ctrl && (x_alu_op == 5'd0);
    assign x_src_sp    = x_is_rr && x_src[5];
    assign x_sp_dst    = x_write && x_is_mov && x_dst[5];   // MOV to a special operand
    assign x_is_ch_rd  = x_src_sp && (x_src[5:2] == 4'b1011);
    assign x_is_rom_rd = x_src_sp && (x_src[5:1] == 5'b10011);
    assign x_is_ch_wr  = x_sp_dst && (x_dst[5:2] == 4'b1011);
    assign x_is_flush  = x_is_ctrl && (x_ctrl_op == CTRL_FLUSH);

    // ================================================================
    // Execute control: one device at a time, level request, ack from device
    // ================================================================
    cond_eval u_xcond (
        .cond_i(x_cond),
        .flags_i({flag_v_r, flag_c_r, flag_n_r, flag_z_r}),
        .ok_o(x_cond_ok)
    );

    assign x_exec        = x_valid_r && x_cond_ok;
    assign t_ch_data_req = x_exec && (x_is_ch_rd || x_is_ch_wr);
    assign t_ch_data_we  = x_is_ch_wr;
    assign t_rom_req     = x_exec && x_is_rom_rd;
    assign t_ch_flush_req = x_exec && x_is_flush;

    assign x_dev_req = t_ch_data_req | t_rom_req | t_ch_flush_req;
    assign x_dev_ack = (t_ch_data_req  & ch_data_ack)
                     | (t_rom_req      & t_rom_ack_r)
                     | (t_ch_flush_req & ch_flush_ack);

    assign x_busy   = x_dev_req & ~x_dev_ack;
    assign x_commit = x_valid_r & ~x_busy;
    assign x_do     = x_commit & x_cond_ok;

    assign adv = ~halted_r & ~x_busy & ~fu_hold;

    // ================================================================
    // Pipeline registers
    // ================================================================
    always_ff @(posedge clk) begin
        if (rst || t_clear)  x_valid_r <= 1'b0;
        else if (!x_busy)    x_valid_r <= adv && fu_d_valid && !x_kill;
        if (adv) x_ins_r <= rom_fetch_data;
    end

    // ================================================================
    // Blocks
    // ================================================================
    microcode_rom #(.DEPTH(4096), .ADDR_W(12), .DATA_W(32),
                    .INIT_FILE("rtl/bootloader.hex")) u_rom (
        .clk(clk),
        .fetch_en_i(adv),
        .fetch_addr_i(fu_pc),
        .fetch_data_o(rom_fetch_data),
        .acc_we_i(t_rom_acc_we),
        .acc_re_i(t_rom_acc_re),
        .acc_addr_i(t_roma_r),
        .acc_wdata_i(t_rom_acc_wdata),
        .acc_rdata_o(rom_acc_rdata)
    );


    fetch_unit u_fu (
        .clk(clk), 
        .rst(rst),
        .clear_i(t_clear),   
        .adv_i(adv), .ir_i(rom_fetch_data),
        .flags_i({flag_v_r, flag_c_r, flag_n_r, flag_z_r}),
        .x_hazard_i(t_fu_x_hazard),
        .x_jump_i(t_fu_x_jump), .x_target_i(t_fu_x_target),
        .csr_we_i(t_fu_csr_we), .csr_sel_i(t_fu_csr_sel), .csr_wdata_i(alu_result),
        .pc_o(fu_pc), .d_valid_o(fu_d_valid), .hold_o(fu_hold),
        .shadow_o(fu_shadow), .loop_cnt_o(fu_loop_cnt), .loop_tgt_o(fu_loop_tgt)
    );

    regfile u_rf (
        .clk(clk), .rd_en_i(adv),
        .opa_addr_i(t_rf_opa_addr), .opa_data_o(rf_opa_data),
        .opb_addr_i(t_rf_opb_addr), .opb_data_o(rf_opb_data),
        .wr_en_i(t_rf_wr_en), .wr_addr_i(x_dst[4:0]), .wr_data_i(alu_result)
    );

    alu u_alu (
        .alu_op(x_alu_op), .a(rf_opa_data), .b(t_alu_b), .carry_in(flag_c_r),
        .result(alu_result), .mul_hi(alu_mul_hi),
        .flag_z(alu_flag_z), .flag_n(alu_flag_n),
        .flag_c(alu_flag_c), .flag_v(alu_flag_v), .upd_cv(alu_upd_cv)
    );
    
    logic ch_preload_busy, ch_preload_done;

    channel_unit u_ch (
        .clk(clk), .rst(rst),
        .preload_i(gpu_start_i), .preload_addr_i(gpu_addr_i),
        .ch_i(t_ch_sel),
        .idx_we_i(t_ch_idx_we), .idxh_we_i(t_ch_idxh_we),
        .step_we_i(t_ch_step_we), .modepage_we_i(t_ch_modepage_we),
        .wdata_i(alu_result),
        .data_req_i(t_ch_data_req), .data_we_i(t_ch_data_we),
        .data_ack_o(ch_data_ack), .data_rdata_o(ch_data_rdata),
        .flush_req_i(t_ch_flush_req), .flush_sel_i(x_addr[1:0]),
        .flush_ack_o(ch_flush_ack),
        .status_o(ch_status), .error_o(ch_error),
        .wb_cyc_o(wb_cyc_o), .wb_stb_o(wb_stb_o), .wb_we_o(wb_we_o),
        .wb_adr_o(wb_adr_o), .wb_dat_o(wb_dat_o),
        .wb_dat_i(wb_dat_i), .wb_ack_i(wb_ack_i),
        .preload_busy_o(ch_preload_busy),
        .preload_done_o(ch_preload_done)
    );

    // ================================================================
    // Operands (D addresses the regfile; X uses its outputs)
    // ================================================================
    assign t_rf_opa_addr = rom_fetch_data[21:17];   // DST[4:0]
    assign t_rf_opb_addr = rom_fetch_data[10:6];    // SRC[4:0]

    always_comb begin
        t_sp_rdata = 16'h0;
        case (x_src)
            6'd33: t_sp_rdata = {12'h0, flag_v_r, flag_c_r, flag_n_r, flag_z_r};
            6'd34: t_sp_rdata = {12'h0, ch_status};
            6'd35: t_sp_rdata = fu_shadow;
            6'd36: t_sp_rdata = fu_loop_cnt;
            6'd37: t_sp_rdata = {4'h0, fu_loop_tgt};
            6'd38: t_sp_rdata = rom_acc_rdata[15:0];
            6'd39: t_sp_rdata = rom_acc_rdata[31:16];
            6'd41: t_sp_rdata = t_mhi_r;
            6'd44, 6'd45, 6'd46, 6'd47: t_sp_rdata = ch_data_rdata;
            default: t_sp_rdata = 16'h0;        // ZERO, W-only, reserved
        endcase
    end

    always_comb begin
        case (x_ins_r[30:29])
            2'b11:   t_alu_b = (x_sx && x_imm11[10]) ? {5'h1F, x_imm11}
                                                     : {5'h00, x_imm11};
            2'b01:   t_alu_b = x_imm16;
            default: t_alu_b = x_src[5] ? t_sp_rdata : rf_opb_data;
        endcase
    end

    // ================================================================
    // Writeback: registers, flags, MHI
    // ================================================================
    assign t_rf_wr_en = x_do && x_write && !x_is_ctrl && !x_dst[5];

    always_ff @(posedge clk) begin
        if (rst || t_clear) begin
            flag_z_r <= 1'b0; flag_n_r <= 1'b0;
            flag_c_r <= 1'b0; flag_v_r <= 1'b0;
        end else if (x_do && x_sp_dst && (x_dst == 6'd33)) begin
            {flag_v_r, flag_c_r, flag_n_r, flag_z_r} <= alu_result[3:0];
        end else if (x_do && x_f && !x_is_ctrl) begin
            flag_z_r <= alu_flag_z;
            flag_n_r <= alu_flag_n;
            if (alu_upd_cv) begin
                flag_c_r <= alu_flag_c;
                flag_v_r <= alu_flag_v;
            end
        end
    end

    always_ff @(posedge clk) begin
        if (rst || t_clear) t_mhi_r <= 16'h0;
        else if (x_do && !x_is_ctrl && (x_alu_op[4:3] == 2'b11)) t_mhi_r <= alu_mul_hi;
    end

    // ================================================================
    // CSR writes, hazards and JMP by register
    // ================================================================
    assign t_fu_csr_we  = x_do && x_sp_dst && (x_dst >= 6'd35) && (x_dst <= 6'd37);
    assign t_fu_csr_sel = (x_dst == 6'd35) ? 2'd0 : (x_dst == 6'd36) ? 2'd1 : 2'd2;

    assign t_fu_x_hazard[0] = x_valid_r && !x_is_ctrl && x_cond_ok &&
                          (x_f || (x_sp_dst && (x_dst == 6'd33)));
    assign t_fu_x_hazard[1] = x_valid_r && x_sp_dst && (x_dst == 6'd35);
    assign t_fu_x_hazard[2] = x_valid_r && x_sp_dst &&
                              (x_dst == 6'd36 || x_dst == 6'd37);

    assign t_fu_x_jump   = x_do && x_is_ctrl && x_r && (x_ctrl_op == CTRL_JMP);
    assign t_fu_x_target = (x_src < 6'd32) ? rf_opb_data[11:0] : t_sp_rdata[11:0];

    // ================================================================
    // ROM access port: ROML / ROMH / ROMA
    // ================================================================
    assign t_romh_wr       = x_do && x_sp_dst && (x_dst == 6'd39);
    assign t_rom_prot_viol = t_romh_wr && (t_roma_r[11:5] == 7'd0);   // words 0..31
    assign t_rom_acc_we    = t_romh_wr && !t_rom_prot_viol;
    assign t_rom_acc_re    = t_rom_req && !t_rom_ack_r;
    assign t_rom_acc_wdata = {alu_result, t_roml_r};

    always_ff @(posedge clk) begin
        if (rst || t_clear) begin
            t_roma_r    <= 12'h0;
            t_roml_r    <= 16'h0;
            t_rom_ack_r <= 1'b0;
        end else begin
            t_rom_ack_r <= t_rom_req && !t_rom_ack_r;    // 1-cycle read latency
            if (x_do && x_sp_dst && (x_dst == 6'd40)) t_roma_r <= alu_result[11:0];
            if (x_do && x_sp_dst && (x_dst == 6'd38)) t_roml_r <= alu_result;
            if (t_rom_acc_we)                                   t_roma_r <= t_roma_r + 12'd1;
            if (x_do && x_is_rom_rd && (x_src == 6'd39))        t_roma_r <= t_roma_r + 12'd1;
        end
    end

    // ================================================================
    // Channel unit control
    // ================================================================
    assign t_ch_sel = x_is_ch_rd ? x_src[1:0] : x_dst[1:0];

    assign t_ch_idx_we      = x_do && x_sp_dst && (x_dst[5:2] == 4'b1100);
    assign t_ch_idxh_we     = x_do && x_sp_dst && (x_dst[5:2] == 4'b1101);
    assign t_ch_step_we     = x_do && x_sp_dst && (x_dst[5:2] == 4'b1110);
    assign t_ch_modepage_we = x_do && x_sp_dst && (x_dst[5:2] == 4'b1111);

    // ================================================================
    // Run control: HALT, IRQ, hardware errors, host START / RESET
    // ================================================================
    assign t_go    = start_i | ch_preload_done;
    assign t_clear = start_i | ch_preload_done | gpu_reset_i;

    always_comb begin
        t_halt_set  = 1'b0;
        t_halt_code = 7'd0;
        if (t_rom_prot_viol) begin
            t_halt_set = 1'b1; t_halt_code = 7'd1;        // write into ROM 0..31
        end else if (ch_error) begin
            t_halt_set = 1'b1; t_halt_code = 7'd2;        // channel left its page
        end else if (x_do && x_is_ctrl && (x_ctrl_op == CTRL_HALT)) begin
            t_halt_set = 1'b1; t_halt_code = x_addr[6:0];
        end
    end

    // drop the word behind a redirect or a halt
    assign x_kill = t_fu_x_jump | t_halt_set;

    always_ff @(posedge clk) begin
        if (rst) begin
            halted_r <= 1'b1; halt_code_r <= 7'h0;
            irq_pending_r <= 1'b0; irq_code_r <= 8'h0;
        end else if (gpu_reset_i) begin
            halted_r <= 1'b1; halt_code_r <= 7'h0; irq_pending_r <= 1'b0;
        end else if (ch_preload_done) begin
            halted_r <= 1'b0; halt_code_r <= 7'h0; irq_pending_r <= 1'b0;
        end else begin
            if (irq_ack_i) irq_pending_r <= 1'b0;
            if (x_do && x_is_ctrl && (x_ctrl_op == CTRL_IRQ)) begin
                irq_pending_r <= 1'b1;
                irq_code_r    <= x_addr[7:0];
            end
            if (t_halt_set && !halted_r) begin
                halted_r    <= 1'b1;
                halt_code_r <= t_halt_code;
            end
        end
    end

    assign halted_o    = halted_r;
    assign error_o     = halted_r && (halt_code_r != 7'd0);
    assign halt_code_o = halt_code_r;
    assign irq_o       = irq_pending_r;
    assign irq_code_o  = irq_code_r;
    assign pc_o        = fu_pc;

endmodule
