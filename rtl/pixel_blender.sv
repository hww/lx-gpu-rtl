`timescale 1ns/1ps
`default_nettype none

module pixel_blender (
    input  logic        clk,
    input  logic        rst,

    input  logic        start,        // импульс: данные word_in валидны (от gc_access_done)
    input  logic        read,         // 1 = read (unpack), 0 = write (pack+blend)
    input  logic [2:0]  mode,
    input  logic [3:0]  pos,
    input  logic [15:0] word_in,
    input  logic [15:0] pixel_in,

    output logic [15:0] pixel_out,
    output logic [15:0] word_out,
    output logic        ack,          // импульс 1 такт: операция завершена
    output logic        busy          // 1 = идёт операция (не готов принимать новую)
);

    // ===== FSM =====
    typedef enum logic [1:0] { B_IDLE, B_CAPTURE, B_FREE } bstate_t;
    bstate_t bstate;

    // Защёлка входов на такте start
    logic        r_read;
    logic [2:0]  r_mode;
    logic [3:0]  r_pos;
    logic [15:0] r_word;
    logic [15:0] r_pixel;

    // Temp-регистр результата
    logic [15:0] r_pixel_out;
    logic [15:0] r_word_out;

    // ===== комбинационная распаковка/упаковка =====
    logic [4:0]  shift;
    logic [15:0] mask;
    logic        is_16bit;
    logic        is_pass;
    logic        write_allowed;

    always_comb begin
        shift  = 5'd0;
        mask   = 16'hFFFF;
        is_16bit = 1'b0;
        is_pass  = 1'b0;

        case (r_mode)
            3'd0: begin
                shift = 5'd15 - {1'b0, r_pos};
                mask  = 16'h0001 << shift;
            end
            3'd1: begin
                shift = ({1'b0, 3'd7} - {1'b0, r_pos[2:0]}) * 2;
                mask  = 16'h0003 << shift;
            end
            3'd2: begin
                shift = ({1'b0, 2'd3} - {1'b0, r_pos[1:0]}) * 4;
                mask  = 16'h000F << shift;
            end
            3'd3: begin
                shift = ({1'b0, 1'b1} - {1'b0, r_pos[0]}) * 8;
                mask  = 16'h00FF << shift;
            end
            3'd4: begin
                shift = 5'd0;
                mask  = 16'hFFFF;
                is_16bit = 1'b1;
            end
            3'd5: begin
                shift = 5'd0;
                mask  = 16'h00FF;
                is_pass = 1'b1;
            end
            3'd6: begin
                shift = 5'd8;
                mask  = 16'hFF00;
                is_pass = 1'b1;
            end
            3'd7: begin
                shift = 5'd0;
                mask  = 16'hFFFF;
                is_pass = 1'b1;
                is_16bit = 1'b1;
            end
        endcase

        write_allowed = ~(r_mode == 3'd0 || r_mode == 3'd1);
    end

    // ===== FSM =====
    always_ff @(posedge clk) begin
        if (rst) begin
            bstate     <= B_IDLE;
            r_read     <= 1'b0;
            r_mode     <= 3'd0;
            r_pos      <= 4'd0;
            r_word     <= 16'd0;
            r_pixel    <= 16'd0;
            r_pixel_out<= 16'd0;
            r_word_out <= 16'd0;
        end else begin
            case (bstate)
                B_IDLE: begin
                    if (start) begin
                        // защёлкиваем входы — данные валидны
                        r_read  <= read;
                        r_mode  <= mode;
                        r_pos   <= pos;
                        r_word  <= word_in;
                        r_pixel <= pixel_in;
                        bstate  <= B_CAPTURE;
                    end
                end

                B_CAPTURE: begin
                    // на этом такте вычисляем результат (комбинационно) и защёлкиваем
                    if (r_read) begin
                        if (is_16bit)       r_pixel_out <= r_word;
                        else if (is_pass)   r_pixel_out <= {8'h0, (r_word & mask) >> shift};
                        else                r_pixel_out <= {12'h0, (r_word & mask) >> shift};
                    end else begin
                        if (!write_allowed) r_word_out <= r_word;
                        else if (is_16bit)  r_word_out <= r_pixel;
                        else                r_word_out <= (r_word & ~mask) | ((r_pixel << shift) & mask);
                    end
                    bstate <= B_FREE;
                end

                B_FREE: begin
                    // ack = 1 такт, результат валиден
                    bstate <= B_IDLE;
                end
            endcase
        end
    end

    // ===== выходы =====
    assign pixel_out = r_pixel_out;
    assign word_out  = r_word_out;
    assign ack       = (bstate == B_FREE);
    assign busy      = (bstate != B_IDLE);

endmodule