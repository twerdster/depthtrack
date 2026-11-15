`timescale 1ns/1ps
// Integration test with the actual thesis_tree_core: complete frame/model
// uploads, 4/5 split inference, synchronous label reads and stale guards.
module tb_tree_protocol_core;
 reg clk=0;always #10 clk=~clk;
 reg reset=1,rx_valid=0,tx_ready=0;reg[7:0]rx_data=0;
 wire rx_ready,tx_valid;wire[7:0]tx_data;wire[15:0]loaded_count;
 tree_protocol #(.WIDTH(8),.HEIGHT(8),.ADDR_WIDTH(6),.TIMEOUT_CYCLES(32)) dut(.*);
 reg[7:0]payload[0:254],response[0:72];integer rlen,seq=0,checks=0;
 reg[15:0]crc,got_crc;reg[7:0]held;integer i,j,n,watchdog;integer run_cycles,base,records,depth;
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
 watchdog=0;while(!tx_valid)begin @(negedge clk);watchdog=watchdog+1;if(watchdog>2000000)$fatal(1,"reply timeout");end
 held=tx_data;repeat(7)begin @(negedge clk);if(!tx_valid||tx_data!==held)$fatal(1,"TX stall changed byte");end
 tx_ready=1;n=0;rlen=-1;watchdog=0;
 while(rlen<0||n<rlen+6)begin
 @(posedge clk);if(tx_valid&&tx_ready)begin
 response[n]=tx_data;n=n+1;if(n==4)rlen=response[3];end
 @(negedge clk);watchdog=watchdog+1;if(watchdog>2000000)$fatal(1,"truncated reply");
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
 request(1,0,0);status(0);
 for(i=0;i<2;i=i+1)begin
 payload[0]=i*32;payload[1]=0;
 for(j=0;j<32;j=j+1)begin payload[2+j*2]=8'he8;payload[3+j*2]=3;end
 if(i==0)begin payload[2+7*2]=0;payload[3+7*2]=0;end
 request(3,66,0);status(0);end
 // All normal nodes choose right, reaching leaf1023 after ten decisions.
 for(base=0;base<1023;base=base+5)begin
 records=((1023-base)>=5)?5:(1023-base);
 payload[0]=0;payload[1]=base%256;payload[2]=base/256;
 for(j=0;j<records*12;j=j+1)payload[j+3]=0;
 for(j=0;j<records;j=j+1)payload[3+j*12+10]=8'h30;
 request(6,3+records*12,0);status(0);end
 for(base=0;base<1024;base=base+63)begin
 records=((1024-base)>=63)?63:(1024-base);
 payload[0]=1;payload[1]=base%256;payload[2]=base/256;
 for(j=0;j<records;j=j+1)payload[j+3]=(base+j)%7;
 request(6,3+records,0);status(0);end
 for(depth=4;depth<=10;depth=depth+1)begin
 payload[0]=depth;request(4,1,0);status(0);
 run_cycles={response[8],response[7],response[6],response[5]};
 if(rlen!=10||run_cycles<=0||{response[12],response[11],response[10],response[9]}!=63)$fatal(1,"real core RUN metrics levels%0d",depth);
 $display("CORE_RUN levels=%0d cycles=%0d valid=63",depth,run_cycles);
 payload[0]=0;payload[1]=0;payload[2]=64;request(5,3,0);status(0);
 for(i=0;i<64;i=i+1)if(response[i+7]!==((i==7)?8'd7:(((1<<depth)-1)%7)))$fatal(1,"real core label levels%0d pixel%0d got%0d",depth,i,response[i+7]);
 end
 // Background7 must remain distinct from a generic no-vote leaf255.
 payload[0]=1;payload[1]=255;payload[2]=3;payload[3]=255;request(6,4,0);status(0);
 if({response[8],response[7]}!=1024)$fatal(1,"leaf1023 overwrite changed prefix");
 payload[0]=0;payload[1]=0;payload[2]=1;request(5,3,0);status(7);
 payload[0]=10;request(4,1,0);status(0);
 payload[0]=0;payload[1]=0;payload[2]=64;request(5,3,0);status(0);
 for(i=0;i<64;i=i+1)if(response[i+7]!==((i==7)?8'd7:8'd255))$fatal(1,"background7/no-vote255 collision pixel%0d",i);
 // Dropping the wrapper prefix must also clear the core's model prefixes.
 request(7,0,0);status(0);
 if(dut.classifier.model_node_count!=0||dut.classifier.model_leaf_count!=0)$fatal(1,"core model_clear not synchronized");
 payload[0]=10;request(4,1,0);status(8);
 request(2,0,0);status(0);payload[0]=0;payload[1]=0;payload[2]=1;request(5,3,0);status(7);
 $display("PASS tree actual-core integration %0d packets: levels4..10,zero centre7,complete1023/1024model,no-vote255,max64Blabelread",checks);$finish;
 end
 initial begin #2000000000;$fatal(1,"watchdog");end
endmodule
