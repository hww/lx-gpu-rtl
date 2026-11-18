
`timescale 1ns/1ps
`default_nettype none

// pixel_engine: per-channel pixel pointer.
// IDX / STEP are stored as 20-bit fixed-point 16.4 in one Simple Dual-Port
// BRAM (8 x 20): {ch,0} = IDX, {ch,1} = STEP. Write-First on read/write
// address collision. Only two working data registers (reg_idx, reg_step).
// word_o / pos_o / mode_o / page_ovf_o always come from reg_idx, never from
// bram_dout, so the address on the cache bus stays stable for the whole
// transaction.
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

    // access handshake
    input  logic        access_i,
    input  logic        access_done_i,   // one-cycle: data ready, advance IDX

    // outputs for the active channel
    output logic [14:0] word_o,
    output logic [3:0]  pos_o,
    output logic [2:0]  mode_o,
    output logic        page_ovf_o,
    output logic        prefetch_o,
    output logic        idx_valid_o      // reg_idx holds a valid IDX for ch_i
);

    localparam int NUM_CH = 4;

    // =====================================================================
    // format shift k
    // =====================================================================
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

    // format used for the shifter: the new mode if it is written this cycle
    logic [2:0] mode_load;
    logic [2:0] k;
    assign mode_load = mode_we_i ? mode_i : mode_r[ch_i]; // <-- ?????????? ? mode_r_active ?? mode_r[ch_i]
    assign k         = k_of_fmt(mode_load);

    // =====================================================================
    // control registers
    // =====================================================================
    logic [2:0]  mode_r [0:NUM_CH-1];
    logic [2:0]  idxh_r [0:NUM_CH-1];
    logic [1:0]  active_ch_r;
    logic        reg_valid_r;
    logic        busy_r;
    logic        prefetch_pend_r;
    logic        idx_loading_r; // <-- ???????? 1 ???? ??? 1-????????? ?????? IDX ?? BRAM

    // =====================================================================
    // BRAM: 8 x 20, Simple Dual-Port, Write-First on collision
    // =====================================================================
    logic [19:0] bram [0:7];

    logic [2:0]  bram_raddr;
    logic [19:0] bram_dout;
    logic [2:0]  bram_waddr;
    logic [19:0] bram_din;
    logic        bram_we;

    always_ff @(posedge clk) begin
        if (bram_we) bram[bram_waddr] <= bram_din;
    end

    // Write-First: the read port returns the new data when addresses collide.
    always_ff @(posedge clk) begin
        if (bram_we && (bram_waddr == bram_raddr))
            bram_dout <= bram_din;
        else
            bram_dout <= bram[bram_raddr];
    end

    // =====================================================================
    // working data registers (the only two)
    // =====================================================================
    logic [19:0] reg_idx;
    logic [19:0] reg_step;

    logic [19:0] next_idx;
    assign next_idx = reg_idx + reg_step;

    // =====================================================================
    // shifter (combinational, no input registers)
    // =====================================================================
    logic [22:0] idx_wide;
    logic [19:0] idx_load;
    assign idx_wide = {idxh_r[ch_i], idx_i, 4'b0} >> k;
    assign idx_load = idx_wide[19:0];

    logic signed [19:0] step_s;
    logic        [19:0] step_load;
    assign step_s    = {{4{step_i[15]}}, step_i};
    assign step_load = step_s >>> k;

    // =====================================================================
    // outputs (strictly from reg_idx)
    // =====================================================================
    logic [2:0] mode_r_active;
    logic [2:0] k_active;
    assign mode_r_active = mode_r[active_ch_r];
    assign k_active      = k_of_fmt(mode_r_active);

    assign word_o     = reg_idx[18:4];
    assign pos_o      = reg_idx[3:0] >> (4 - k_active);
    assign mode_o     = mode_r_active;
    assign page_ovf_o = reg_idx[19];

    // valid address for the current channel (busy_r not included: the cache
    // must see a valid address on the cycle the access starts)
    assign idx_valid_o = reg_valid_r && (active_ch_r == ch_i);

    // =====================================================================
    // BRAM write port mux
    // =====================================================================
    logic        idx_accept;
    logic        step_accept;
    logic        auto_inc;

    assign idx_accept  = idx_we_i;
    assign step_accept = step_we_i;
    assign auto_inc    = access_done_i && !idx_accept && !step_accept;

    always_comb begin
        bram_we    = 1'b0;
        bram_waddr = 3'd0;
        bram_din   = 20'd0;

        if (idx_accept) begin
            bram_we    = 1'b1;
            bram_waddr = {ch_i, 1'b0};
            bram_din   = idx_load;
        end else if (step_accept) begin
            bram_we    = 1'b1;
            bram_waddr = {ch_i, 1'b1};
            bram_din   = step_load;
        end else if (auto_inc) begin
            bram_we    = 1'b1;
            bram_waddr = {active_ch_r, 1'b0};
            bram_din   = next_idx;
        end
    end

    // =====================================================================
    // BRAM read port mux
    // =====================================================================
    logic reg_loaded_prev;
    always_comb begin
        if (idx_we_i || (idx_loading_r && (active_ch_r == ch_i)))
            bram_raddr = {ch_i, 1'b1};   // next: STEP
        else
            bram_raddr = {ch_i, 1'b0};   // next: IDX
    end

    // =====================================================================
    // control
    // =====================================================================
    logic start_access;
    assign start_access = access_i && idx_valid_o && !busy_r;

    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) begin
                mode_r[i] <= 3'd0;
                idxh_r[i] <= 3'd0;
            end
            active_ch_r     <= 2'd0;
            reg_valid_r     <= 1'b0;
            busy_r          <= 1'b0;
            prefetch_pend_r <= 1'b0;
            reg_idx         <= 20'd0;
            reg_step        <= 20'd0;
            reg_loaded_prev <= 1'b0;
            idx_loading_r   <= 1'b0;
        end else begin
            // ---------------------------------------------------------
            // reg_idx loading
            // ---------------------------------------------------------
            if (idx_we_i && !busy_r) begin
                // <-- ????? idx_load ???????? (0 ?????? ????????), ? STEP ??? ????????
                reg_idx         <= idx_load;
                active_ch_r     <= ch_i;
                reg_valid_r     <= 1'b1;
                reg_loaded_prev <= 1'b1;
                idx_loading_r   <= 1'b0;
            end else if (!busy_r && (!reg_valid_r || (active_ch_r != ch_i))) begin
                reg_loaded_prev <= 1'b0;
                if (!idx_loading_r || (active_ch_r != ch_i)) begin
                    active_ch_r   <= ch_i;
                    reg_valid_r   <= 1'b0;
                    idx_loading_r <= 1'b1;
                end else begin
                    reg_idx         <= bram_dout;
                    reg_valid_r     <= 1'b1;
                    idx_loading_r   <= 1'b0;
                    reg_loaded_prev <= 1'b1;
                end
            end

            if (step_we_i && (ch_i == active_ch_r) && !busy_r) begin
                reg_step        <= step_load;
                reg_loaded_prev <= 1'b0;
            end else if (reg_loaded_prev && !(!busy_r && (active_ch_r != ch_i))) begin
                reg_step        <= bram_dout;
                reg_loaded_prev <= 1'b0;
            end

            // ---------------------------------------------------------
            // reg_idx update at the end of a cache transaction
            // ---------------------------------------------------------
            if (access_done_i && busy_r) begin
                reg_idx         <= next_idx;
                busy_r          <= 1'b0;
                reg_valid_r     <= 1'b1;
                prefetch_pend_r <= (next_idx[18:4] != reg_idx[18:4]);
            end

            // start of a new cache transaction
            if (start_access) begin
                busy_r <= 1'b1;
            end

            // ---------------------------------------------------------
            // prefetch strobe & reset on activity
            // ---------------------------------------------------------
            if (access_i || idx_we_i || (active_ch_r != ch_i))
                prefetch_pend_r <= 1'b0;
            else if (prefetch_pend_r && (active_ch_r == ch_i))
                prefetch_pend_r <= 1'b0;

            // ---------------------------------------------------------
            // IDXH: one-shot, cleared by the IDX write
            // ---------------------------------------------------------
            for (int i = 0; i < NUM_CH; i++) begin
                if (idxh_we_i && ch_i == i[1:0])
                    idxh_r[i] <= idxh_i;
                else if (idx_we_i && ch_i == i[1:0])
                    idxh_r[i] <= 3'h0;
            end
            // ---------------------------------------------------------
            // MODE
            // ---------------------------------------------------------
            if (mode_we_i)
                mode_r[ch_i] <= mode_i;
        end
    end
    // prefetch_o: one-cycle strobe
    logic prefetch_r;
    assign prefetch_o = prefetch_r;
    always_ff @(posedge clk) begin
        if (rst)
            prefetch_r <= 1'b0;
        else
            prefetch_r <= prefetch_pend_r && !access_i && !idx_we_i && (active_ch_r == ch_i);
    end
endmodule
