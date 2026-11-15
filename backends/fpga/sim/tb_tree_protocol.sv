`timescale 1ns/1ps
// Transport unit test: the stub below records memory writes and provides a
// deterministic delayed result. It does not validate classification arithmetic.
module tb_tree_protocol;
 reg clk=0;always #10 clk=~clk;
 reg reset=1,rx_valid=0,tx_ready=0;reg[7:0]rx_data=0;
 wire rx_ready,tx_valid;wire[7:0]tx_data;wire[15:0]loaded_count;
 tree_protocol #(.WIDTH(8),.HEIGHT(8),.ADDR_WIDTH(6),.TIMEOUT_CYCLES(32)) dut(.*);
 reg[7:0]payload[0:254],response[0:72];integer rlen,seq=0,checks=0;
 reg[15:0]crc,got_crc;reg[7:0]held;integer i,j,n,watchdog,base,records;
 function[15:0] cb(input[15:0]a,input[7:0]d);reg[15:0]c;integer b;begin
 c=a^{d,8'b0};for(b=0;b<8;b=b+1)c=c[15]?((c<<1)^16'h1021):(c<<1);cb=c;end endfunction
 task byte_in(input[7:0]d);begin
 @(negedge clk);rx_data=d;rx_valid=1;
 while(!rx_ready)@(negedge clk);
 @(negedge clk);rx_valid=0;
 end endtask
 task request(input[7:0]cmd,input integer len,input integer bad_crc);integer k;reg[15:0]c;begin
 seq=(seq+1)&255;c=16'hffff;byte_in(8'ha5);byte_in(cmd);c=cb(c,cmd);
 byte_in(seq);c=cb(c,seq);byte_in(len);c=cb(c,len);
 for(k=0;k<len;k=k+1)begin byte_in(payload[k]);c=cb(c,payload[k]);end
 byte_in(c[7:0]^(bad_crc?8'h01:8'h00));byte_in(c[15:8]);
 // Stalled TX must hold both data and valid until accepted.
 watchdog=0;while(!tx_valid)begin @(negedge clk);watchdog=watchdog+1;if(watchdog>20000)$fatal(1,"reply timeout");end
 held=tx_data;repeat(7)begin @(negedge clk);if(!tx_valid||tx_data!==held)$fatal(1,"TX stall changed byte");end
 tx_ready=1;n=0;rlen=-1;watchdog=0;
 while(rlen<0||n<rlen+6)begin
 @(posedge clk);if(tx_valid&&tx_ready)begin
 response[n]=tx_data;n=n+1;if(n==4)rlen=response[3];end
 @(negedge clk);watchdog=watchdog+1;if(watchdog>20000)$fatal(1,"truncated reply");
 end
 tx_ready=0;
 if(response[0]!==8'h5a||response[1]!== (cmd|8'h80)||response[2]!==seq[7:0])$fatal(1,"reply header");
 c=16'hffff;for(k=1;k<rlen+4;k=k+1)c=cb(c,response[k]);
 if({response[rlen+5],response[rlen+4]}!==c)$fatal(1,"reply CRC");
 checks=checks+1;
 end endtask
 task status(input[7:0]v);begin if(response[4]!==v)$fatal(1,"status got%0d expected%0d",response[4],v);end endtask
 initial begin
 repeat(4)@(negedge clk);reset=0;
 request(1,0,0);status(0);if(rlen!=15||response[5]!=4||response[7]!=8||response[9]!=8||response[17]!=10||response[11]!=64||response[12]!=0)$fatal(1,"PING4");
 payload[0]=4;request(4,1,0);status(5);
 payload[0]=0;payload[1]=0;payload[2]=1;request(3,3,0);status(3);
 payload[0]=1;payload[1]=0;payload[2]=8'hab;payload[3]=8'hcd;request(3,4,0);status(4);
 payload[0]=0;request(3,4,1);status(1);if(loaded_count!=0)$fatal(1,"bad CRC write side effect");
 for(i=0;i<2;i=i+1)begin
 payload[0]=i*32;payload[1]=0;for(j=0;j<32;j=j+1)begin payload[2+j*2]=(i*32+j);payload[3+j*2]=8'h12;end
 request(3,66,0);status(0);end
 if(loaded_count!=64)$fatal(1,"depth count");
 for(i=0;i<64;i=i+1)if(dut.classifier.frame[i]!==16'h1200+i)$fatal(1,"depth word packing %d",i);
 payload[0]=4;request(4,1,0);status(8);
 // Three 5-node packets produce all 15 required depth4 internal nodes.
 for(i=0;i<3;i=i+1)begin
 payload[0]=0;payload[1]=i*5;payload[2]=0;for(j=0;j<60;j=j+1)payload[j+3]=i*60+j;
 request(6,63,0);status(0);end
 if(dut.classifier.nodes[0]!==96'h0b0a09080706050403020100)$fatal(1,"node endianness");
 payload[0]=1;payload[1]=0;payload[2]=0;for(j=0;j<32;j=j+1)payload[j+3]=j%7;
 request(6,35,0);status(0);if(response[5]!=15||response[6]!=0||response[7]!=32||response[8]!=0)$fatal(1,"model counts");
 payload[0]=5;request(4,1,0);status(8);
 payload[0]=4;request(4,1,0);status(0);
 if(rlen!=10||{response[8],response[7],response[6],response[5]}!=1234||response[9]!=64)$fatal(1,"RUN metrics");
 payload[0]=0;payload[1]=0;payload[2]=64;request(5,3,0);status(0);
 if(rlen!=67)$fatal(1,"max response length");for(i=0;i<64;i=i+1)if(response[i+7]!==i%7)$fatal(1,"sync label read %0d",i);
 // An idempotent depth rewrite invalidates the preceding result.
 payload[0]=0;payload[1]=0;payload[2]=8'h44;payload[3]=8'h33;request(3,4,0);status(0);
 if(loaded_count!=64||dut.classifier.frame[0]!==16'h3344)$fatal(1,"rewrite");
 payload[2]=1;request(5,3,0);status(7);
 request(7,0,0);status(0);payload[0]=4;request(4,1,0);status(8);
 // Load all1023 nodes and1024 leaves, crossing the8bit boundary.
 for(base=0;base<1023;base=base+5)begin
 records=((1023-base)>=5)?5:(1023-base);
 payload[0]=0;payload[1]=base%256;payload[2]=base/256;
 for(j=0;j<records*12;j=j+1)payload[j+3]=0;
 for(j=0;j<records;j=j+1)begin payload[3+j*12]=(base+j)%256;payload[4+j*12]=(base+j)/256;payload[13+j*12]=8'h20;end
 request(6,3+records*12,0);status(0);end
 for(base=0;base<1024;base=base+63)begin
 records=((1024-base)>=63)?63:(1024-base);
 payload[0]=1;payload[1]=base%256;payload[2]=base/256;
 for(j=0;j<records;j=j+1)payload[j+3]=(base+j)%256;
 request(6,3+records,0);status(0);end
 request(1,0,0);status(0);
 if({response[14],response[13]}!=1023||{response[16],response[15]}!=1024)$fatal(1,"16bit model PING counts");
 if(dut.classifier.nodes[255][15:0]!==16'd255||dut.classifier.nodes[256][15:0]!==16'd256||dut.classifier.nodes[1022][15:0]!==16'd1022)$fatal(1,"10bit node address packing");
 if(dut.classifier.leaves[255]!==8'hff||dut.classifier.leaves[256]!==8'h00||dut.classifier.leaves[1023]!==8'hff)$fatal(1,"10bit leaf address packing");
 payload[0]=10;request(4,1,0);status(0);
 payload[0]=11;request(4,1,0);status(3);
 payload[0]=0;payload[1]=255;payload[2]=3;for(j=0;j<12;j=j+1)payload[j+3]=0;
 request(6,15,0);status(4); // node1023 is beyond the1023-node heap
 payload[0]=1;payload[1]=0;payload[2]=4;payload[3]=1;request(6,4,0);status(4);
 request(7,0,0);status(0);if(dut.classifier.clear_count!=2)$fatal(1,"MODEL_BEGIN must pulse model_clear");
 payload[0]=10;request(4,1,0);status(8);
 // Oversized request is fully drained, rejected, then parser resynchronizes.
 for(i=0;i<70;i=i+1)payload[i]=i;request(3,70,0);status(3);request(1,0,0);status(0);
 byte_in(8'ha5);byte_in(8'h03);repeat(40)@(negedge clk);request(1,0,0);status(0);
 request(2,0,0);status(0);if(loaded_count!=0)$fatal(1,"BEGIN");
 $display("PASS tree protocol %0d packets: CRC16, guards, depth/model writes, sync64B labels, TX stalls, timeout",checks);$finish;
 end
 initial begin #100000000;$fatal(1,"watchdog");end
endmodule

module thesis_tree_core #(parameter WIDTH=8,HEIGHT=8,ADDR_WIDTH=6,MAX_LEVELS=10,CONTEXTS=4)(
 input clk,reset,frame_we,input[ADDR_WIDTH-1:0]frame_addr,input[15:0]frame_data,
 input model_clear,model_we,model_is_leaf,input[9:0]model_addr,input[95:0]model_data,
 input start,input[3:0]levels,output reg busy,done,error,output reg[31:0]cycles,valid_pixels,
 input[ADDR_WIDTH-1:0]label_addr,output reg[7:0]label_data);
 reg[15:0]frame[0:WIDTH*HEIGHT-1];reg[95:0]nodes[0:1022];reg[7:0]leaves[0:1023];integer remaining,clear_count;
 always@(posedge clk)begin
 label_data<=label_addr%7;
 if(reset)begin busy<=0;done<=0;error<=0;cycles<=0;valid_pixels<=0;remaining<=0;clear_count<=0;end
 else begin
 done<=0;
 if(model_clear)clear_count<=clear_count+1;
 if(frame_we&&!busy)frame[frame_addr]<=frame_data;
 if(model_we&&!busy)begin if(model_is_leaf)leaves[model_addr]<=model_data[7:0];else nodes[model_addr]<=model_data;end
 if(start&&!busy)begin busy<=1;remaining<=9;end
 if(busy)begin if(remaining==0)begin busy<=0;done<=1;cycles<=1234;valid_pixels<=WIDTH*HEIGHT;end else remaining<=remaining-1;end
 end end
endmodule
