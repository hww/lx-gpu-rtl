`timescale 1ns/1ps
module tb_top;

    logic clk = 0, rst = 1;
    logic halted, error;

    // GPU memory bus
    logic        gpu_req, gpu_we;
    logic [23:0] gpu_addr;
    logic [15:0] gpu_wdata;
    logic [1:0]  gpu_sel;
    logic        gpu_ack0, gpu_ack1;
    logic [15:0] gpu_rdata;

    top dut (
        .clk(clk), .rst(rst),
        .halted(halted), .error(error),
        .gpu_req(gpu_req), .gpu_we(gpu_we), .gpu_addr(gpu_addr),
        .gpu_wdata(gpu_wdata), .gpu_sel(gpu_sel),
        .gpu_ack0(gpu_ack0), .gpu_ack1(gpu_ack1), .gpu_rdata(gpu_rdata)
    );

    memory_arbiter_stub #(.MEM_WORDS(32768)) u_arb (
        .clk(clk), .rst(rst),
        .gpu_addr_i(gpu_addr), .gpu_req_i(gpu_req),
        .gpu_we_i(gpu_we), .gpu_dat_i(gpu_wdata),
        .gpu_dat_o(gpu_rdata),
        .gpu_ack0_o(gpu_ack0), .gpu_ack1_o(gpu_ack1),
        .gpu_sel_i(gpu_sel)
    );

    always #5 clk = ~clk;

    // instruction packer
    function automatic logic [31:0] pack_insn(
        input logic        l,
        input logic [1:0]  imm_class,
        input logic [4:0]  dst,
        input logic        ds,
        input logic [3:0]  cond,
        input logic        f,
        input logic [4:0]  op,
        input logic        sx,
        input logic [11:0] imm12,
        input logic [5:0]  src,
        input logic [23:0] imm24
    );
        logic [31:0] v;
        v = {l, imm_class, dst, ds, cond, f, op, sx, imm12};
        if (imm_class == 2'b00 || imm_class == 2'b10)
            v = (v & ~32'h1F80) | ({26'h0, src} << 7);
        if (imm_class == 2'b01)
            v = (v & 32'hFF00_0000) | {8'h0, imm24};
        pack_insn = v;
    endfunction

    function automatic logic [31:0] I_imm(
        input logic [4:0]  dst,
        input logic [11:0] imm,
        input logic [4:0]  op,
        input logic [3:0]  cond = 4'd0,
        input logic        f    = 1'b0,
        input logic        l    = 1'b0,
        input logic        sx   = 1'b0
    );
        I_imm = pack_insn(l, 2'b11, dst, 1'b0, cond, f, op, sx, imm, 6'h0, 24'h0);
    endfunction

    function automatic logic [31:0] I_ctrl(
        input logic [4:0]  op,
        input logic [11:0] addr,
        input logic [3:0]  cond = 4'd0
    );
        I_ctrl = pack_insn(1'b0, 2'b10, 5'd0, 1'b0, cond, 1'b0, op, 1'b0, addr, 6'h0, 24'h0);
    endfunction

    initial begin
        // program:
        //  0: MOV DST.PAGE,  #0    (reg-imm12, dst_code=50, imm=0)
        //  1: MOV DST.MODE,  #2    (dst_code=49, imm=2 = 4bpp)
        //  2: MOV DST.STEP,  #1    (dst_code=48)
        //  3: MOV DST.IDX,   #0    (dst_code=45)
        //  4: MOV DST.PIX,   #0xA  (dst_code=46, imm=0xA)
        //  5: MOV FLUSH,     ZERO  (dst_code=57, src=ZERO)
        //  6: HALT 0
        dut.rom.mem[0] = I_imm(5'd18, 12'd0,  5'd0);  // dst=18 (50&31), ds=1? -> need ds=1
        // Note: I_imm uses dst 5-bit; special needs ds=1.
        // For simplicity, build full 32-bit words manually below.

        // word 0: MOV DST.PAGE, #0
        // imm_class=11, dst=18 (50&31), ds=1, op=MOV(0), imm12=0
        dut.rom.mem[0] = {1'b0, 2'b11, 5'd18, 1'b1, 4'd0, 1'b0, 5'd0, 1'b0, 12'd0};
        // word 1: MOV DST.MODE, #2
        dut.rom.mem[1] = {1'b0, 2'b11, 5'd17, 1'b1, 4'd0, 1'b0, 5'd0, 1'b0, 12'd2};
        // word 2: MOV DST.STEP, #1
        dut.rom.mem[2] = {1'b0, 2'b11, 5'd16, 1'b1, 4'd0, 1'b0, 5'd0, 1'b0, 12'd1};
        // word 3: MOV DST.IDX, #0
        dut.rom.mem[3] = {1'b0, 2'b11, 5'd13, 1'b1, 4'd0, 1'b0, 5'd0, 1'b0, 12'd0};
        // word 4: MOV DST.PIX, #0xA
        dut.rom.mem[4] = {1'b0, 2'b11, 5'd14, 1'b1, 4'd0, 1'b0, 5'd0, 1'b0, 12'hA};
        // word 5: MOV FLUSH, ZERO  (reg-reg, dst=25=57&31 ds=1, src=32=ZERO, op=MOV)
        dut.rom.mem[5] = {1'b0, 2'b00, 5'd25, 1'b1, 4'd0, 1'b0, 5'd0, 1'b0, 12'h0};
        // src=32 -> in [12:7]: 32<<7 = 0x1000
        dut.rom.mem[5] = dut.rom.mem[5] | (32 << 7);
        // word 6: HALT 0
        dut.rom.mem[6] = {1'b0, 2'b10, 5'd0, 1'b0, 4'd0, 1'b0, 5'd5, 1'b0, 12'h0};

        // preload memory word 0 (byte 0) with 0xFFFF
        u_arb.mem[0] = 16'hFFFF;

        repeat (2) @(posedge clk);
        rst = 0;

        // run until halted
        begin : run_loop
            repeat (500) begin
                @(posedge clk);
                if (halted) disable run_loop;
                if (error) begin
                    $error("top error, code=%h", dut.error_code);
                    disable run_loop;
                end
            end
        end

        // check memory word 0: expected 0xAFFF (pos 0 = high nibble)
        if (u_arb.mem[0] !== 16'hAFFF)
            $error("mem[0] = %h, expected AFFF", u_arb.mem[0]);
        if (!halted)
            $error("CPU not halted");

        $display("tb_top: PASS");
        $finish;
    end

endmodule