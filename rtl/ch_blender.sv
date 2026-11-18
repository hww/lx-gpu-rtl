`timescale 1ns/1ps
`default_nettype none

module ch_blender
    import gpu_fmt_pkg::*;
(
    input  logic        clk,
    input  logic        rst,

    // Wishbone slave (от channel_unit)
    input  logic        cyc_i,
    input  logic        stb_i,
    input  logic        we_i,        // 0 = read, 1 = write
    input  logic [2:0]  mode_i,      // из pixel_engine
    input  logic [3:0]  frac_i,      // из pixel_engine (reg_idx[3:0])
    input  logic [15:0] dat_i,       // слово из кэша (cache_rdata)
    input  logic [15:0] pixel_i,     // пиксель для записи от процессора
    output logic [15:0] dat_o,       // распакованный пиксель (read) / слово (write)
    output logic        ack_o,       // 1-тактный строб завершения

    // RMW handshake к кэшу
    input  logic        ack_i,       // cache_ack
    output logic        rmw_req_o,   // запрос к кэшу
    output logic        rmw_we_o,    // 0 = read, 1 = write
    output logic [15:0] rmw_dat_o    // смешанное слово для записи в кэш
);

    // =====================================================================
    // READ: комбинаторная распаковка из frac_i
    // =====================================================================
    fmt_info_t   fmt_c;
    logic [3:0]  sh_c;
    logic [15:0] pixel_c;

    assign fmt_c   = get_fmt_info(mode_i);
    assign sh_c    = ~frac_i & fmt_c.s_mask;
    assign pixel_c = (dat_i >> sh_c) & fmt_c.pix_mask;

    // =====================================================================
    // WRITE: RMW FSM (IDLE -> RMW_READ -> RMW_WRITE -> IDLE)
    // =====================================================================
    typedef enum logic [1:0] { IDLE, RMW_READ, RMW_WRITE } state_t;
    state_t state;

    logic [2:0]  mode_r;
    logic [3:0]  frac_r;
    logic [15:0] word_r, pixel_r;
    logic        ack_r;

    fmt_info_t   fmt_r;
    logic [3:0]  sh_r;
    logic [15:0] mask_r;
    logic [15:0] blend_r;

    assign fmt_r   = get_fmt_info(mode_r);
    assign sh_r    = ~frac_r & fmt_r.s_mask;
    assign mask_r  = fmt_r.pix_mask << sh_r;
    assign blend_r = fmt_r.wr_en ? ((word_r & ~mask_r) | ((pixel_r << sh_r) & mask_r))
                                 : word_r; // защита от записи в 1bpp/2bpp

    always_ff @(posedge clk) begin
        if (rst) begin
            state   <= IDLE;
            mode_r  <= 3'd0;
            frac_r  <= 4'd0;
            word_r  <= 16'd0;
            pixel_r <= 16'd0;
            ack_r   <= 1'b0;
        end else begin
            ack_r <= 1'b0;

            case (state)
                IDLE: begin
                    if (cyc_i && stb_i && we_i && !ack_r) begin   
                        mode_r  <= mode_i;
                        frac_r  <= frac_i;
                        pixel_r <= pixel_i;
                        state   <= RMW_READ;
                    end
                end

                RMW_READ: begin
                    if (ack_i) begin
                        word_r <= dat_i;
                        state  <= RMW_WRITE;
                    end
                end

                RMW_WRITE: begin
                    if (ack_i) begin
                        ack_r <= 1'b1;
                        state <= IDLE;
                    end
                end

                default: state <= IDLE;
            endcase
        end
    end

    // При чтении: запрос идёт напрямую в кэш (rmw_req_o не нужен, ack_o = ack_i)
    // При записи: RMW-автомат делает чтение (RMW_READ) и запись (RMW_WRITE)
    assign ack_o     = (cyc_i && stb_i && !we_i) ? ack_i : ack_r;
    assign dat_o     = (cyc_i && stb_i && !we_i) ? pixel_c : blend_r;

    assign rmw_req_o = (state == RMW_READ) || (state == RMW_WRITE);
    assign rmw_we_o  = (state == RMW_WRITE);
    assign rmw_dat_o = blend_r;

endmodule
