`timescale 1ns/1ps
module top (
    input  logic clk,
    input  logic rst,
    output logic halted,
    output logic error,

    output logic        gpu_req,
    output logic        gpu_we,
    output logic [23:0] gpu_addr,
    output logic [15:0] gpu_wdata,
    output logic [1:0]  gpu_sel,
    input  logic        gpu_ack0,
    input  logic        gpu_ack1,
    input  logic [15:0] gpu_rdata,
    input  logic        rom_we,
    input  logic [11:0] rom_addr,
    input  logic [31:0] rom_wdata,
    input  logic        start
);

    // ===== microcode ROM =====
    logic [11:0] pc;
    logic [31:0] insn;
    microcode_rom #(.DEPTH(4096), .INIT_FILE("")) rom (
        .clk(clk), .addr(pc), .data(insn),
        .we(rom_we), .waddr(rom_addr), .wdata(rom_wdata)
    );

    // ===== unpack instruction =====
    logic        l, ds, f, sx;
    logic [1:0]  imm_class;
    logic [4:0]  dst, reg_op;
    logic [3:0]  cond;
    logic [5:0]  src;
    logic [11:0] imm12;
    logic [23:0] imm24;

    assign l         = insn[31];
    assign imm_class = {insn[30], insn[29]};
    assign dst       = insn[28:24];
    assign ds        = insn[23];
    assign cond      = insn[22:19];
    assign f         = insn[18];
    assign reg_op    = insn[17:13];
    assign src       = insn[12:7];
    assign imm12     = insn[11:0];
    assign imm24     = insn[23:0];
    assign sx        = insn[12];

    // ===== flags =====
    logic z_r, n_r, c_r, v_r;
    logic z, n, c, v;
    assign z = z_r; assign n = n_r; assign c = c_r; assign v = v_r;

    // ===== regfile =====
    logic [4:0]  ra, rb, wa;
    logic [23:0] rdata_a, rdata_b, wdata;
    logic        we;
    regfile rf (
        .clk(clk), .rst(rst),
        .ra(ra), .rdata_a(rdata_a),
        .rb(rb), .rdata_b(rdata_b),
        .we(we), .wa(wa), .wdata(wdata)
    );

    // ===== ALU =====
    logic [23:0] alu_a, alu_b, alu_res;
    logic        alu_z, alu_n, alu_c, alu_v;
    alu al (
        .op(reg_op), .a(alu_a), .b(alu_b),
        .res(alu_res), .z(alu_z), .n(alu_n), .c(alu_c), .v(alu_v)
    );

    // ===== FSM type (declared early so stall can use it) =====
    typedef enum logic [1:0] {
        FSM_IDLE, FSM_MEM_REQ, FSM_MEM_WAIT, FSM_WRITE
    } fsm_t;
    fsm_t fsm;

    logic [1:0]  op_ch;
    logic [5:0]  op_code;
    logic [15:0] op_pix;
    
    logic        stall;
    assign stall = (fsm != FSM_IDLE) || pb_wb_req;

    // ===== loop_we/loop_in =====
    logic        loop_we;
    logic [23:0] loop_in;
    assign loop_we = 1'b0;
    assign loop_in = 24'h0;

    // ===== sequencer =====
    logic [11:0] pc_next, error_code;
    logic [15:0] shadow;
    logic [23:0] loop_reg;
    sequencer seq (
        .clk(clk), .rst(rst), .start(start),
        .imm_class(imm_class), .reg_op(reg_op), .cond(cond),
        .l(l), .imm12(imm12), .imm24(imm24),
        .z(z_r), .n(n_r), .c(c_r), .v(v_r),
        .pc(pc), .pc_next(pc_next), .shadow(shadow),
        .loop_reg(loop_reg), .halted(halted), .error(error),
        .error_code(error_code),
        .loop_we(loop_we), .loop_in(loop_in), .stall(stall)
    );

    // ===== channel state =====
    logic [2:0]  ch_mode [0:3];
    logic [7:0]  ch_page [0:3];
    logic [23:0] ch_idx  [0:3];
    logic [23:0] ch_step [0:3];

    // ===== pixel_buffer =====
    logic [1:0]  pb_ch;
    logic [23:0] pb_addr;
    logic        pb_hit;
    logic [15:0] pb_rdata;
    logic        pb_wb_req;
    logic [23:0] pb_wb_addr;
    logic [15:0] pb_wb_data;
    logic        pb_wb_ack;
    logic        pb_fill_en;
    logic [15:0] pb_fill_data;
    logic        pb_we;
    logic [15:0] pb_wdata;
    logic        pb_set_page_en;
    logic [7:0]  pb_set_page_val;
    logic        pb_flush_en;
    logic        pb_error;

    pixel_buffer pb (
        .clk(clk), .rst(rst),
        .ch(pb_ch), .addr24(pb_addr),
        .hit(pb_hit), .rdata(pb_rdata),
        .fill_en(pb_fill_en), .fill_data(pb_fill_data),
        .we(pb_we), .wdata(pb_wdata),
        .set_page_en(pb_set_page_en), .set_page_val(pb_set_page_val),
        .flush_en(pb_flush_en),
        .wb_req(pb_wb_req), .wb_addr(pb_wb_addr), .wb_data(pb_wb_data),
        .wb_ack(pb_wb_ack),
        .error(pb_error)
    );

    // ===== addr_packer (SRC/DST/STM pixel addressing) =====
    logic [23:0] ap_idx;
    logic [7:0]  ap_page;
    logic [2:0]  ap_fmt;
    logic [23:0] ap_addr;
    logic [3:0]  ap_pos;
    addr_packer ap (
        .idx(ap_idx), .page(ap_page), .fmt(ap_fmt),
        .addr24(ap_addr), .pos(ap_pos)
    );

    // ===== pixel_packer (read) =====
    logic [2:0]  pp_fmt_r;
    logic [3:0]  pp_pos_r;
    logic [15:0] pp_word_r;
    logic [15:0] pp_pix_r;
    logic [15:0] pp_word_w_unused;
    logic        pp_is_write_unused;
    pixel_packer ppr (
        .fmt(pp_fmt_r), .pos(pp_pos_r), .word(pp_word_r),
        .pixel_in(16'h0),
        .pixel_out(pp_pix_r), .word_out(pp_word_w_unused),
        .is_write(pp_is_write_unused)
    );

    // ===== pixel_packer (write) =====
    logic [2:0]  pp_fmt_w;
    logic [3:0]  pp_pos_w;
    logic [15:0] pp_word_w;
    logic [15:0] pp_pix_w;
    logic [15:0] pp_pix_w_unused;
    logic [15:0] pp_word_w_out;
    logic        pp_is_write_w;
    pixel_packer ppw (
        .fmt(pp_fmt_w), .pos(pp_pos_w), .word(pp_word_w),
        .pixel_in(pp_pix_w),
        .pixel_out(pp_pix_w_unused), .word_out(pp_word_w_out),
        .is_write(pp_is_write_w)
    );

    // ===== operand codes =====
    logic [5:0]  dst_code, src_code;
    assign dst_code = {ds, dst};
    assign src_code = src;

    // ===== classification =====
    logic is_ctrl, is_ldi, is_cmp_tst, sp_write;
    assign is_ctrl    = (imm_class == 2'b10);
    assign is_ldi     = (imm_class == 2'b01);
    assign is_cmp_tst = (reg_op == 5'd7) || (reg_op == 5'd8);
    assign sp_write   = (!is_ctrl && !is_ldi && dst_code[5]);

    // ===== write-path addr_packer (uses op_ch) =====
    logic [23:0] wp_idx;
    logic [7:0]  wp_page;
    logic [2:0]  wp_fmt;
    logic [23:0] wp_addr;
    logic [3:0]  wp_pos;
    assign wp_idx  = ch_idx[op_ch];
    assign wp_page = ch_page[op_ch];
    assign wp_fmt  = ch_mode[op_ch];
    addr_packer apw (
        .idx(wp_idx), .page(wp_page), .fmt(wp_fmt),
        .addr24(wp_addr), .pos(wp_pos)
    );

    // write packer: uses wp_fmt / wp_pos / current buffer word
    always_comb begin
        pp_fmt_w  = wp_fmt;
        pp_pos_w  = wp_pos;
        pp_word_w = pb_rdata;
        pp_pix_w  = (fsm == FSM_WRITE) ? op_pix : alu_res[15:0];
    end

    // ===== special operand read =====
    logic [23:0] sp_rd;
    logic [23:0] op_a, op_b;

    always_comb begin
        // defaults
        sp_rd      = 24'h0;
        pb_ch      = 2'd0;
        pb_addr    = 24'h0;
        pb_set_page_en = 1'b0;
        pb_set_page_val = 8'h0;
        pb_flush_en = 1'b0;
        ap_idx  = 24'h0;
        ap_page = 8'h0;
        ap_fmt  = 3'd0;
        pp_fmt_r = 3'd0;
        pp_pos_r = 4'd0;
        pp_word_r = 16'h0;

        case (src_code)
            6'd32: sp_rd = 24'h0;
            6'd33: sp_rd = {20'h0, v, c, n, z};
            6'd34: sp_rd = {8'h0, shadow};
            6'd35: sp_rd = loop_reg;
            6'd36, 6'd37, 6'd38: sp_rd = 24'h0;
            6'd39: sp_rd = ch_idx[0];
            6'd40: begin
                ap_idx  = ch_idx[0];
                ap_page = ch_page[0];
                ap_fmt  = ch_mode[0];
                pb_ch   = 2'd0;
                pb_addr = ap_addr;
                pp_fmt_r = ch_mode[0];
                pp_pos_r = ap_pos;
                pp_word_r = pb_rdata;
                sp_rd = {8'h0, pp_pix_r};
            end
            6'd41: begin
                pb_ch   = 2'd0;
                pb_addr = {ch_page[0], ch_idx[0][15:0]} << 1;
                sp_rd = {8'h0, pb_rdata};
            end
            6'd42: sp_rd = ch_step[0];
            6'd43: sp_rd = {21'h0, ch_mode[0]};
            6'd44: sp_rd = {16'h0, ch_page[0]};
            6'd45: sp_rd = ch_idx[1];
            6'd46: begin
                ap_idx  = ch_idx[1];
                ap_page = ch_page[1];
                ap_fmt  = ch_mode[1];
                pb_ch   = 2'd1;
                pb_addr = ap_addr;
                pp_fmt_r = ch_mode[1];
                pp_pos_r = ap_pos;
                pp_word_r = pb_rdata;
                sp_rd = {8'h0, pp_pix_r};
            end
            6'd47: begin
                pb_ch   = 2'd1;
                pb_addr = {ch_page[1], ch_idx[1][15:0]} << 1;
                sp_rd = {8'h0, pb_rdata};
            end
            6'd48: sp_rd = ch_step[1];
            6'd49: sp_rd = {21'h0, ch_mode[1]};
            6'd50: sp_rd = {16'h0, ch_page[1]};
            6'd51: sp_rd = ch_idx[2];
            6'd52: begin
                pb_ch   = 2'd2;
                pb_addr = {ch_page[2], ch_idx[2][15:0]} << 1;
                sp_rd = {8'h0, pb_rdata};
            end
            6'd53: begin
                pb_ch   = 2'd2;
                pb_addr = {ch_page[2], ch_idx[2][15:0]} << 1;
                sp_rd = {8'h0, pb_rdata};
            end
            6'd54: sp_rd = ch_step[2];
            6'd55: sp_rd = {21'h0, ch_mode[2]};
            6'd56: sp_rd = {16'h0, ch_page[2]};
            6'd57: sp_rd = 24'h0;
            6'd58: begin
                ap_idx  = ch_idx[2];
                ap_page = ch_page[2];
                ap_fmt  = ch_mode[2];
                pb_ch   = 2'd2;
                pb_addr = ap_addr;
                pp_fmt_r = ch_mode[2];
                pp_pos_r = ap_pos;
                pp_word_r = pb_rdata;
                sp_rd = {8'h0, pp_pix_r};
            end
            default: sp_rd = 24'h0;
        endcase

        // FSM override: fill/write use op_ch (the channel being written)
        if (fsm == FSM_MEM_REQ || fsm == FSM_WRITE) begin
            pb_ch   = op_ch;
            pb_addr = wp_addr;
        end

        // special writes (PAGE / FLUSH)
        if (sp_write) begin
            case (dst_code)
                6'd44: begin pb_set_page_en = 1'b1; pb_set_page_val = alu_res[7:0]; end
                6'd50: begin pb_set_page_en = 1'b1; pb_set_page_val = alu_res[7:0]; end
                6'd56: begin pb_set_page_en = 1'b1; pb_set_page_val = alu_res[7:0]; end
                6'd57: if (fsm == FSM_IDLE) pb_flush_en = 1'b1;
                default: ;
            endcase
        end
    end

    // ===== GPU bus =====
    assign gpu_req   = pb_wb_req || (fsm == FSM_MEM_REQ);
    assign gpu_we    = pb_wb_req;
    assign gpu_addr  = pb_wb_req ? pb_wb_addr : pb_addr;
    assign gpu_wdata = pb_wb_req ? pb_wb_data : pp_word_w_out;
    assign gpu_sel   = 2'b11;
    assign pb_wb_ack = pb_wb_req && gpu_ack1;

    // fill / write strobes
    assign pb_fill_en   = (fsm == FSM_MEM_REQ) && gpu_ack1;
    assign pb_fill_data = gpu_rdata;
    assign pb_we        = (fsm == FSM_WRITE);
    assign pb_wdata     = pp_word_w_out;

    // ===== operand read mux =====
    always_comb begin
        if (dst_code[5] == 1'b0) begin
            ra  = dst_code[4:0];
            op_a = rdata_a;
        end else begin
            ra  = 5'd0;
            op_a = sp_rd;
        end

        if (imm_class == 2'b00) begin
            if (src_code[5] == 1'b0) begin
                rb  = src_code[4:0];
                op_b = rdata_b;
            end else begin
                rb  = 5'd0;
                op_b = sp_rd;
            end
        end else begin
            rb  = 5'd0;
            op_b = sx && imm12[11] ? {12'hFFF, imm12} : {12'h000, imm12};
        end
    end

    assign alu_a = (imm_class == 2'b01) ? 24'h0 : op_a;
    assign alu_b = op_b;

    // ===== write-back =====
    always_comb begin
        we    = 1'b0;
        wa    = 5'd0;
        wdata = 24'h0;

        $display("WB: is_ctrl=%b is_ldi=%b dst_code=%d alu_res=%h op_a=%h op_b=%h imm12=%h imm24=%h",
                 is_ctrl, is_ldi, dst_code, alu_res, op_a, op_b, imm12, imm24);

        if (!is_ctrl && !is_ldi) begin
            if (!is_cmp_tst && dst_code[5] == 1'b0) begin
                we    = 1'b1;
                wa    = dst_code[4:0];
                wdata = alu_res;
            end
        end else if (is_ldi && !dst_code[5]) begin
            we    = 1'b1;
            wa    = dst;
            wdata = imm24;
        end
    end

    // ===== channel state / FSM =====
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < 4; i++) begin
                ch_mode[i] <= 3'd0;
                ch_page[i] <= 8'd0;
                ch_idx[i]  <= 24'd0;
                ch_step[i] <= 24'd0;
            end
            z_r <= 0; n_r <= 0; c_r <= 0; v_r <= 0;
            fsm <= FSM_IDLE;
        end else begin
            if (!is_ctrl) begin
                if (f || is_cmp_tst) begin
                    z_r <= alu_z; n_r <= alu_n; c_r <= alu_c; v_r <= alu_v;
                end
            end

            case (fsm)
                FSM_IDLE: begin
                                        $display("IDLE: ch_idx1=%h ch_mode1=%d", ch_idx[1], ch_mode[1]);
                    if (sp_write) begin
                        case (dst_code)
                            6'd39: ch_idx[0] <= alu_res;
                            6'd42: ch_step[0] <= alu_res;
                            6'd43: ch_mode[0] <= alu_res[2:0];
                            6'd44: ch_page[0] <= alu_res[7:0];
                            6'd45: begin ch_idx[1] <= alu_res; $display("SET DST.IDX: alu_res=%h", alu_res); end
                            6'd48: ch_step[1] <= alu_res;
                            6'd49: ch_mode[1] <= alu_res[2:0];
                            6'd50: ch_page[1] <= alu_res[7:0];
                            6'd51: ch_idx[2] <= alu_res;
                            6'd54: ch_step[2] <= alu_res;
                            6'd55: ch_mode[2] <= alu_res[2:0];
                            6'd56: ch_page[2] <= alu_res[7:0];
                            default: ;
                        endcase
                    end

                    if (sp_write && (dst_code == 6'd46 || dst_code == 6'd47)) begin
                        op_ch   <= 2'd1;
                        op_code <= dst_code;
                        op_pix  <= alu_res[15:0];
                        fsm     <= pb_hit ? FSM_WRITE : FSM_MEM_REQ;
                    end else if (src_code == 6'd40 && !is_ctrl && !is_ldi) begin
                        ch_idx[0] <= ch_idx[0] + ch_step[0];
                    end
                end

                FSM_MEM_REQ: begin
                    if (gpu_ack1) fsm <= FSM_WRITE;
                end

                FSM_MEM_WAIT: begin
                    // unused
                end

                FSM_WRITE: begin
                    if (op_code == 6'd46) begin
                        ch_idx[1] <= ch_idx[1] + ch_step[1];
                    end else if (op_code == 6'd47) begin
                        ch_idx[1] <= ch_idx[1] + 24'd1;
                    end
                    if (gpu_ack1 || !pb_hit) begin
                        // wait one extra cycle so fill is visible
                    end
                    fsm <= FSM_IDLE;
                end
            endcase
        end
    end

endmodule