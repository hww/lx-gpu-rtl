module microcode_rom #(
    parameter int DEPTH = 4096,
    parameter       INIT_FILE = ""
) (
    input  logic        clk,
    input  logic [11:0] addr,        // read address (PC)
    output logic [31:0] data,        // read data
    input  logic        we,          // debug write
    input  logic [11:0] waddr,
    input  logic [31:0] wdata
);
    logic [31:0] mem [0:DEPTH-1];
    initial begin
        if (INIT_FILE != "") $readmemh(INIT_FILE, mem);
        else for (int i = 0; i < DEPTH; i++) mem[i] = 32'h0;
    end
    always_ff @(posedge clk) 
        if (we) begin 
            mem[waddr] <= wdata;
        end
    assign data = mem[addr];

    always_comb $display("ROM read: addr=%h data=%h mem0=%h", addr, data, mem[0]);
endmodule