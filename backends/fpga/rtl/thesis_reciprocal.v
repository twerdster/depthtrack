// R=floor(65536/z). For z>=2, start with remainder1 then divide16 zero bits.
// z=1 returns0 as an explicit bypass code interpreted by the coordinate pipe.
module thesis_reciprocal (
    input wire clk,input wire reset,input wire start,input wire [15:0] z,
    output reg busy,output reg done,output reg [15:0] reciprocal
);
    reg [16:0] remainder;
    reg [15:0] divisor,quotient;
    reg [3:0] step;
    wire [16:0] shifted={remainder[15:0],1'b0};
    wire subtract=shifted>={1'b0,divisor};
    wire [16:0] next_remainder=subtract ? shifted-{1'b0,divisor} : shifted;
    wire [15:0] next_quotient={quotient[14:0],subtract};
    always @(posedge clk) begin
        if(reset) begin busy<=0;done<=0;reciprocal<=0;remainder<=0;divisor<=0;quotient<=0;step<=0;end
        else begin
            done<=0;
            if(start && !busy) begin
                if(z<=1) begin reciprocal<=0;done<=1;end
                else begin busy<=1;remainder<=1;divisor<=z;quotient<=0;step<=0;end
            end else if(busy) begin
                remainder<=next_remainder;quotient<=next_quotient;
                if(step==15) begin reciprocal<=next_quotient;busy<=0;done<=1;end
                else step<=step+1'b1;
            end
        end
    end
endmodule
