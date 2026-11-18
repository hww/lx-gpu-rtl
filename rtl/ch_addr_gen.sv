`timescale 1ns/1ps
`default_nettype none

module ch_addr_gen (
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
    input  logic        access_done_i,

    // outputs for the active channel
    output logic [14:0] word_o,
    output logic [3:0]  frac_o,      // сырая дробная часть reg_idx[3:0] без сдвига!
    output logic [2:0]  mode_o,
    output logic        page_ovf_o,
    output logic        prefetch_o,
    output logic        idx_valid_o
);

    `include "rtl/gpu_fmt_pkg.sv"

    localparam int NUM_CH = 4;

    logic [2:0] mode_r [0:NUM_CH-1];
    logic [2:0] idxh_r [0:NUM_CH-1];

    logic [2:0] mode_load;
    fmt_info_t  fmt_load;
    logic [2:0] k;

    assign mode_load = mode_we_i ? mode_i : mode_r[ch_i];
    assign fmt_load  = get_fmt_info(mode_load);
    assign k         = fmt_load.k;

    // =====================================================================
    // Общий 23-битный сдвигатель со знаком для IDX и STEP (в целых пикселях)
    // =====================================================================
    logic signed [22:0] shift_in;
    logic signed [22:0] shift_out;
    logic        [19:0] shifted_val;

    always_comb begin
        if (idx_we_i)
            shift_in = $signed({idxh_r[ch_i], idx_i, 4'b0});
        else
            shift_in = $signed({{3{step_i[15]}}, step_i, 4'b0});
    end

    assign shift_out   = shift_in >>> k;
    assign shifted_val = shift_out[19:0];

    // =====================================================================
    // control registers & BRAM
    // =====================================================================
    logic [1:0]  active_ch_r;
    logic        reg_valid_r;
    logic        busy_r;
    logic        prefetch_pend_r;
    logic        idx_loading_r;

    logic [19:0] bram [0:7];
    logic [2:0]  bram_raddr;
    logic [19:0] bram_dout;
    logic [2:0]  bram_waddr;
    logic [19:0] bram_din;
    logic        bram_we;

    always_ff @(posedge clk) begin
        if (bram_we) bram[bram_waddr] <= bram_din;
    end

    always_ff @(posedge clk) begin
        if (bram_we && (bram_waddr == bram_raddr))
            bram_dout <= bram_din;
        else
            bram_dout <= bram[bram_raddr];
    end

    logic [19:0] reg_idx;
    logic [19:0] reg_step;
    logic [19:0] next_idx;
    assign next_idx = reg_idx + reg_step;

    // =====================================================================
    // outputs (напрямую из reg_idx без сдвигателей)
    // =====================================================================
    assign word_o      = reg_idx[18:4];
    assign frac_o      = reg_idx[3:0];
    assign mode_o      = mode_r[active_ch_r];
    assign page_ovf_o  = reg_idx[19];
    assign idx_valid_o = reg_valid_r && (active_ch_r == ch_i);

    // =====================================================================
    // BRAM write / read port mux
    // =====================================================================
    logic idx_accept, step_accept, auto_inc;
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
            bram_din   = shifted_val;
        end else if (step_accept) begin
            bram_we    = 1'b1;
            bram_waddr = {ch_i, 1'b1};
            bram_din   = shifted_val;
        end else if (auto_inc) begin
            bram_we    = 1'b1;
            bram_waddr = {active_ch_r, 1'b0};
            bram_din   = next_idx;
        end
    end

    logic reg_loaded_prev;
    always_comb begin
        if (idx_we_i || (idx_loading_r && (active_ch_r == ch_i)))
            bram_raddr = {ch_i, 1'b1};
        else
            bram_raddr = {ch_i, 1'b0};
    end

    // =====================================================================
    // control FSM
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
            // reg_idx loading (процессор пишет регистры только когда канал не в транзакции)
            // ---------------------------------------------------------
            if (idx_we_i) begin
                reg_idx         <= shifted_val;
                active_ch_r     <= ch_i;
                reg_valid_r     <= 1'b1;
                reg_loaded_prev <= 1'b1;
                idx_loading_r   <= 1'b0;
                busy_r          <= 1'b0;
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

            if (step_we_i && (ch_i == active_ch_r)) begin
                reg_step        <= shifted_val;
                reg_loaded_prev <= 1'b0;
            end else if (reg_loaded_prev && !(!busy_r && (active_ch_r != ch_i))) begin
                reg_step        <= bram_dout;
                reg_loaded_prev <= 1'b0;
            end

            if (access_done_i) begin
                reg_idx         <= next_idx;
                busy_r          <= 1'b0;
                reg_valid_r     <= 1'b1;
                prefetch_pend_r <= (next_idx[18:4] != reg_idx[18:4]);
            end

            if (start_access) begin
                busy_r <= 1'b1;
            end

            if (access_i || idx_we_i || (active_ch_r != ch_i))
                prefetch_pend_r <= 1'b0;
            else if (prefetch_pend_r && (active_ch_r == ch_i))
                prefetch_pend_r <= 1'b0;

            for (int i = 0; i < NUM_CH; i++) begin
                if (idxh_we_i && ch_i == i[1:0])
                    idxh_r[i] <= idxh_i;
                else if (idx_we_i && ch_i == i[1:0])
                    idxh_r[i] <= 3'h0;
            end

            if (mode_we_i)
                mode_r[ch_i] <= mode_i;
        end
    end

    logic prefetch_r;
    assign prefetch_o = prefetch_r;
    always_ff @(posedge clk) begin
        if (rst)
            prefetch_r <= 1'b0;
        else
            prefetch_r <= prefetch_pend_r && !access_i && !idx_we_i && (active_ch_r == ch_i);
    end

endmodule
