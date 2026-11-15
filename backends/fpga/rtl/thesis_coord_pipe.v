// Exact integer-profile coordinate: trunc_zero(base + signed_offset / z).
// Nonzero uint16 depth; signed16 offset. Five registered stages, II=1.
// R=floor(65536/z) for z>=2, z=1 bypass. Two 16x16 DSP products.
module thesis_coord_pipe (
    input wire clk, input wire reset, input wire in_valid,
    input wire [15:0] base, input wire signed [15:0] offset,
    input wire [15:0] z, input wire [15:0] reciprocal,
    output reg out_valid, output reg signed [17:0] coordinate
);
    reg v1,v2,v3,v4;
    wire [15:0] magnitude = offset[15] ? (~offset+16'd1) : offset;
    reg [31:0] product1, product2;
    reg [15:0] n1,n2,z1,z2,base1,base2,base3;
    reg neg1,neg2,neg3,neg4;
    reg [15:0] q2,q3;
    reg nonzero3,nonzero4;
    reg signed [17:0] sum4;
    wire [16:0] residual = {1'b0,n2}-{1'b0,product2[15:0]};
    wire correct = residual >= {1'b0,z2};
    wire [16:0] exact_remainder = correct ? residual-{1'b0,z2} : residual;
    wire signed [17:0] signed_q3 = neg3 ? -$signed({2'b00,q3}) : $signed({2'b00,q3});
    always @(posedge clk) begin
        if(reset) begin
            v1<=0;v2<=0;v3<=0;v4<=0;out_valid<=0;coordinate<=0;
            product1<=0;product2<=0;n1<=0;n2<=0;z1<=0;z2<=0;
            base1<=0;base2<=0;base3<=0;neg1<=0;neg2<=0;neg3<=0;neg4<=0;
            q2<=0;q3<=0;nonzero3<=0;nonzero4<=0;sum4<=0;
        end else begin
            v1<=in_valid; v2<=v1; v3<=v2; v4<=v3; out_valid<=v4;
            if(in_valid) begin
                product1<=magnitude*reciprocal;
                n1<=magnitude;z1<=z;base1<=base;neg1<=offset[15];
            end
            if(v1) begin
                q2<=product1[31:16];product2<=product1[31:16]*z1;
                n2<=n1;z2<=z1;base2<=base1;neg2<=neg1;
            end
            if(v2) begin
                q3<=z2==1 ? n2 : q2+correct;
                nonzero3<=z2==1 ? 1'b0 : exact_remainder!=0;
                base3<=base2;neg3<=neg2;
            end
            if(v3) begin
                sum4<=$signed({2'b00,base3})+signed_q3;
                nonzero4<=nonzero3;neg4<=neg3;
            end
            if(v4) coordinate<=neg4 && nonzero4 && sum4>0 ? sum4-18'sd1 : sum4;
        end
    end
endmodule
