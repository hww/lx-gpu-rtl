`timescale 1ns/1ps
`default_nettype none

// pixel_blender: unpack a pixel from a word (read) or pack a pixel into a word
// (write = read-modify-write blend).
// Inputs are captured on start_i; the result is valid while ack_o = 1.
// Latency: start_i -> ST_COMPUTE -> ST_DONE (ack_o = 1 for one cycle).
//
// All formats support both read and write:
//   0 = 1bpp (16 pixels/word), 1 = 2bpp (8), 2 = 4bpp (4),
//   3 = 8bpp (2), 4 = 16bpp (1), 5 = PASS_LO8, 6 = PASS_HI8, 7 = PASS16.
module pixel_blender (
    input  logic        clk,
    input  logic        rst,

    input  logic        start_i,     // pulse: word_i is valid (cache access_done)
    input  logic        is_read_i,   // 1 = read (unpack), 0 = write (pack + blend)
    input  logic [2:0]  mode_i,
    input  logic [3:0]  pos_i,
    input  logic [15:0] word_i,
    input  logic [15:0] pixel_i,

    output logic [15:0] pixel_o,
    output logic [15:0] word_o,
    output logic        ack_o,       // one-cycle pulse: operation finished
    output logic        busy_o       // 1 = operation in progress
);

    typedef enum logic [1:0] { ST_IDLE, ST_COMPUTE, ST_DONE } state_t;
    state_t state;

    // inputs captured on start_i
    logic        is_read_r;
    logic [2:0]  mode_r;
    logic [3:0]  pos_r;
    logic [15:0] word_r;
    logic [15:0] pixel_r;

    // result registers
    logic [15:0] pixel_res_r;
    logic [15:0] word_res_r;

    // ===== combinational unpack / pack =====
    logic [4:0]  shift;
    logic [15:0] mask;
    logic        full_word;     // one pixel = whole word
    logic        is_pass;       // PASS_LO8 / PASS_HI8 / PASS16

    always_comb begin
        shift     = 5'd0;
        mask      = 16'hFFFF;
        full_word = 1'b0;
        is_pass   = 1'b0;

        case (mode_r)
            3'd0: begin   // 1 bpp
                shift = 5'd15 - {1'b0, pos_r};
                mask  = 16'h0001 << shift;
            end
            3'd1: begin   // 2 bpp
                shift = ({1'b0, 3'd7} - {1'b0, pos_r[2:0]}) * 2;
                mask  = 16'h0003 << shift;
            end
            3'd2: begin   // 4 bpp
                shift = ({2'b0, 2'd3} - {2'b0, pos_r[1:0]}) * 4;
                mask  = 16'h000F << shift;
            end
            3'd3: begin   // 8 bpp
                shift = ({3'b0, 1'b1} - {3'b0, pos_r[0]}) * 8;
                mask  = 16'h00FF << shift;
            end
            3'd4: begin   // 16 bpp
                full_word = 1'b1;
            end
            3'd5: begin   // PASS_LO8
                mask    = 16'h00FF;
                is_pass = 1'b1;
            end
            3'd6: begin   // PASS_HI8
                shift   = 5'd8;
                mask    = 16'hFF00;
                is_pass = 1'b1;
            end
            3'd7: begin   // PASS16
                full_word = 1'b1;
                is_pass   = 1'b1;
            end
        endcase
    end

    // ===== FSM =====
    always_ff @(posedge clk) begin
        if (rst) begin
            state       <= ST_IDLE;
            is_read_r   <= 1'b0;
            mode_r      <= 3'd0;
            pos_r       <= 4'd0;
            word_r      <= 16'd0;
            pixel_r     <= 16'd0;
            pixel_res_r <= 16'd0;
            word_res_r  <= 16'd0;
        end else begin
            case (state)
                ST_IDLE: begin
                    if (start_i) begin
                        is_read_r <= is_read_i;
                        mode_r    <= mode_i;
                        pos_r     <= pos_i;
                        word_r    <= word_i;
                        pixel_r   <= pixel_i;
                        state     <= ST_COMPUTE;
                    end
                end

                ST_COMPUTE: begin
                    if (is_read_r) begin
                        if (full_word)    pixel_res_r <= word_r;
                        else if (is_pass) pixel_res_r <= 16'({8'h0,  (word_r & mask) >> shift});
                        else              pixel_res_r <= 16'({12'h0, (word_r & mask) >> shift});
                    end else begin
                        if (full_word)    word_res_r <= pixel_r;
                        else              word_res_r <= (word_r & ~mask) | ((pixel_r << shift) & mask);
                    end
                    state <= ST_DONE;
                end

                ST_DONE: state <= ST_IDLE;   // ack_o is high in this state

                default: state <= ST_IDLE;
            endcase
        end
    end

    assign pixel_o = pixel_res_r;
    assign word_o  = word_res_r;
    assign ack_o   = (state == ST_DONE);
    assign busy_o  = (state != ST_IDLE);

endmodule
