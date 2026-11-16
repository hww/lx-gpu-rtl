`timescale 1ns/1ps
`default_nettype none

// pixel_blender: Wishbone slave.
//
//   adr_i[0] : 0 = read (unpack), 1 = write (pack + RMW)
//   adr_i[1] : channel (kept for RMW; value is informational for top)
//   dat_i    : word from cache (read: word_i, write: old word for RMW)
//   pixel_i  : ALU result (write only)
//   dat_o    : pixel (read) or blended word (write)
//   ack_o    : 1-cycle (read: cyc&stb&~we comb; write: at end of RMW)
//
// RMW write: FSM IDLE -> RMW_READ -> RMW_WRITE -> IDLE.
//   The blender is the WB slave to top; top is the WB master to the cache.
//   cyc_o/stb_o/we_o are not driven here; top builds them.
//   ack_i (cache access_done) is provided by top for the RMW handshake.
module pixel_blender (
    input  logic        clk,
    input  logic        rst,

    // Wishbone slave (from top)
    input  logic        cyc_i,
    input  logic        stb_i,
    input  logic        we_i,        // 0 = read, 1 = write
    input  logic [2:0]  adr_i,       // [0] = read/write, [1] = channel (info)
    input  logic [2:0]  mode_i,      // from pe (mode of selected channel)
    input  logic [3:0]  pos_i,       // from pe (pixel pos)
    input  logic [15:0] dat_i,       // word (cache rdata)
    input  logic [15:0] pixel_i,     // ALU result (write)
    output logic [15:0] dat_o,       // pixel (read) / blended word (write)
    output logic        ack_o,       // 1-cycle

    // RMW handshake to top (cache access_done)
    input  logic        ack_i,       // cache access_done (RMW step done)
    output logic        rmw_req_o,   // 1 = blender requests a cache access
    output logic        rmw_we_o,    // 1 = write (RMW_WRITE), 0 = read (RMW_READ)
    output logic [15:0] rmw_dat_o    // write data (blend) during RMW_WRITE
);

    // =====================================================================
    // READ: combinational unpack
    // =====================================================================
    logic [4:0]  shift_c;
    logic [15:0] mask_c;
    logic        full_c, pass_c;

    always_comb begin
        shift_c = 5'd0;
        mask_c  = 16'hFFFF;
        full_c  = 1'b0;
        pass_c  = 1'b0;

        case (mode_i)
            3'd0: begin  // 1 bpp
                shift_c = 5'd15 - {1'b0, pos_i};
                mask_c  = 16'h0001 << shift_c;
            end
            3'd1: begin  // 2 bpp
                shift_c = ({1'b0, 3'd7} - {1'b0, pos_i[2:0]}) * 2;
                mask_c  = 16'h0003 << shift_c;
            end
            3'd2: begin  // 4 bpp
                shift_c = ({2'b0, 2'd3} - {2'b0, pos_i[1:0]}) * 4;
                mask_c  = 16'h000F << shift_c;
            end
            3'd3: begin  // 8 bpp
                shift_c = ({3'b0, 1'b1} - {3'b0, pos_i[0]}) * 8;
                mask_c  = 16'h00FF << shift_c;
            end
            3'd4: full_c = 1'b1;
            3'd5: begin mask_c = 16'h00FF; pass_c = 1'b1; end
            3'd6: begin shift_c = 5'd8; mask_c = 16'hFF00; pass_c = 1'b1; end
            3'd7: begin full_c = 1'b1; pass_c = 1'b1; end
        endcase
    end

    logic [15:0] pixel_c;
    always_comb begin
        if (full_c)      pixel_c = dat_i;
        else             pixel_c = (dat_i & mask_c) >> shift_c;
    end

    // =====================================================================
    // WRITE: RMW FSM
    // =====================================================================
    typedef enum logic [1:0] { IDLE, RMW_READ, RMW_WRITE } state_t;
    state_t state;

    logic [2:0]  mode_r;
    logic [3:0]  pos_r;
    logic [15:0] word_r, pixel_r;
    logic        ack_r;

    logic [4:0]  shift_r;
    logic [15:0] mask_r;
    logic        full_r, pass_r;

    always_comb begin
        shift_r = 5'd0;
        mask_r  = 16'hFFFF;
        full_r  = 1'b0;
        pass_r  = 1'b0;

        case (mode_r)
            3'd0: begin
                shift_r = 5'd15 - {1'b0, pos_r};
                mask_r  = 16'h0001 << shift_r;
            end
            3'd1: begin
                shift_r = ({1'b0, 3'd7} - {1'b0, pos_r[2:0]}) * 2;
                mask_r  = 16'h0003 << shift_r;
            end
            3'd2: begin
                shift_r = ({2'b0, 2'd3} - {2'b0, pos_r[1:0]}) * 4;
                mask_r  = 16'h000F << shift_r;
            end
            3'd3: begin
                shift_r = ({3'b0, 1'b1} - {3'b0, pos_r[0]}) * 8;
                mask_r  = 16'h00FF << shift_r;
            end
            3'd4: full_r = 1'b1;
            3'd5: begin mask_r = 16'h00FF; pass_r = 1'b1; end
            3'd6: begin shift_r = 5'd8; mask_r = 16'hFF00; pass_r = 1'b1; end
            3'd7: begin full_r = 1'b1; pass_r = 1'b1; end
        endcase
    end

    logic [15:0] blend_r;
    always_comb begin
        if (full_r) blend_r = pixel_r;
        else        blend_r = (word_r & ~mask_r) | ((pixel_r << shift_r) & mask_r);
    end

    // =====================================================================
    // FSM
    // =====================================================================
    always_ff @(posedge clk) begin
        if (rst) begin
            state   <= IDLE;
            mode_r  <= 3'd0;
            pos_r   <= 4'd0;
            word_r  <= 16'd0;
            pixel_r <= 16'd0;
            ack_r   <= 1'b0;
        end else begin
            ack_r <= 1'b0;

            case (state)
                IDLE: begin
                    if (cyc_i && stb_i && we_i) begin
                        mode_r  <= mode_i;
                        pos_r   <= pos_i;
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

    // =====================================================================
    // Outputs
    // =====================================================================
    // read: combinational ack (1-cycle while cyc&stb)
    // write: ack_r (1-cycle at end of RMW)
    assign ack_o  = (cyc_i && stb_i && !we_i) ? ack_i
                  : ack_r;

    assign dat_o  = (cyc_i && stb_i && !we_i) ? pixel_c
                  : blend_r;

    assign rmw_req_o = (state == RMW_READ) || (state == RMW_WRITE);
    assign rmw_we_o  = (state == RMW_WRITE);
    assign rmw_dat_o = blend_r;

endmodule
