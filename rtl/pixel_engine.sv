`timescale 1ns/1ps
`default_nettype none

// pixel_engine: per-channel pixel pointer.
// Holds IDX / STEP (stored as {word[14:0], frac[3:0]}), MODE and IDXH for each
// of the 4 channels. Writes are converted from pixel units to {word, frac}
// by the format shift k. After each access IDX += STEP (one shared adder).
// Outputs the word address, the pixel position inside the word and the mode.
//
// NOTE: idx_step_mem is FF-based (async read, full reset) — ~150 FF, not EBR.
module pixel_engine (
    input  logic        clk,
    input  logic        rst,

    input  logic [1:0]  ch_i,

    // channel register writes
    input  logic        mode_we_i,
    input  logic [2:0]  mode_i,
    input  logic        idxh_we_i,
    input  logic [2:0]  idxh_i,
    input  logic        idx_we_i,
    input  logic [15:0] idx_i,
    input  logic        step_we_i,
    input  logic [15:0] step_i,

    // access handshake from the cache
    input  logic        access_i,
    input  logic        access_done_i,   // one-cycle pulse: data ready, advance IDX

    // outputs for the selected channel
    output logic [14:0] word_o,
    output logic [3:0]  pos_o,
    output logic [2:0]  mode_o,
    output logic        prefetch_o
);

    localparam int NUM_CH = 4;

    // 8 entries x 19 bits: {ch, 0} = IDX, {ch, 1} = STEP
    logic [18:0] idx_step_mem [0:7];
    logic [18:0] idx_cur, step_cur;

    logic [2:0]  mode_r [0:NUM_CH-1];
    logic [2:0]  idxh_r [0:NUM_CH-1];

    // pixels per word as a power of two: 1 bpp = 16 ... 16 bpp = 1
    function automatic logic [2:0] k_of_fmt(input logic [2:0] fmt);
        case (fmt)
            3'd0:    k_of_fmt = 3'd4;  // 1 bpp
            3'd1:    k_of_fmt = 3'd3;  // 2 bpp
            3'd2:    k_of_fmt = 3'd2;  // 4 bpp
            3'd3:    k_of_fmt = 3'd1;  // 8 bpp
            3'd4:    k_of_fmt = 3'd0;  // 16 bpp
            default: k_of_fmt = 3'd0;  // PASS_*
        endcase
    endfunction

    // format used for the shift: the new mode if it is written this cycle
    logic [2:0] mode_load;
    logic [2:0] k;
    logic [3:0] frac_shift;
    assign mode_load  = mode_we_i ? mode_i : mode_r[ch_i];
    assign k          = k_of_fmt(mode_load);
    assign frac_shift = 4 - k;

    assign idx_cur  = idx_step_mem[{ch_i, 1'b0}];
    assign step_cur = idx_step_mem[{ch_i, 1'b1}];

    // word address = integer part, pixel position = frac >> (4 - k)
    assign word_o = idx_cur[18:4];
    assign pos_o  = idx_cur[3:0] >> frac_shift;
    assign mode_o = mode_r[ch_i];

    // IDX write: pixel index {idxh, idx_i} -> {word, frac} = (index << 4) >> k
    logic [22:0] idx_wide;
    logic [18:0] idx_load;
    assign idx_wide = {idxh_r[ch_i], idx_i, 4'b0} >> k;
    assign idx_load = idx_wide[18:0];

    // STEP write: 12.4 sign-extended to 15.4, then arithmetic >> k
    logic signed [18:0] step_s;
    logic        [18:0] step_load;
    assign step_s    = {{3{step_i[15]}}, step_i};
    assign step_load = step_s >>> k;

    // auto-increment
    logic [18:0] idx_next;
    assign idx_next = idx_cur + step_cur;

    always_ff @(posedge clk) begin
        if (rst) begin
            for (int j = 0; j < 8; j++) idx_step_mem[j] <= 19'h0;
        end else begin
            if (idx_we_i) begin
`ifdef SIMULATION
                $display("IDX write: ch=%d mode=%d k=%d idx=%h load=%h",
                         ch_i, mode_r[ch_i], k, idx_i, idx_load);
`endif
                idx_step_mem[{ch_i, 1'b0}] <= idx_load;
            end else if (access_done_i) begin
                idx_step_mem[{ch_i, 1'b0}] <= idx_next;
            end
            if (step_we_i) idx_step_mem[{ch_i, 1'b1}] <= step_load;
        end
    end

    // MODE registers
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) mode_r[i] <= 3'd0;
        end else if (mode_we_i) begin
            mode_r[ch_i] <= mode_i;
        end
    end

    // IDXH registers (one-shot: cleared by the next IDX write)
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) idxh_r[i] <= 3'h0;
        end else begin
            for (int i = 0; i < NUM_CH; i++) begin
                if (idxh_we_i && ch_i == i[1:0])     idxh_r[i] <= idxh_i;
                else if (idx_we_i && ch_i == i[1:0]) idxh_r[i] <= 3'h0;
            end
        end
    end

    // prefetch: word changes after auto-increment or IDX write
    logic [14:0] word_next;
    assign word_next  = idx_next[18:4];
    assign prefetch_o = idx_we_i || (access_done_i && (word_next != word_o));

endmodule
