`timescale 1ns/1ps
// Serial-bit test using the manufacturer UART and the local AVR bridge copy.
// No actual board is connected. AVR acknowledgement is modeled as a toggle,
// deliberately delayed 1500 clocks after each decoded response byte.
module tb_tree_uart;
    reg clk=0, rst_n=0, cclk=0;
    always #10 clk=~clk;
    reg avr_tx=1, avr_rx_busy=0;
    reg ack_enabled=1;
    wire avr_rx, spi_miso;
    wire [3:0] spi_channel;
    wire [7:0] led;
    mojo_top dut(.clk(clk),.rst_n(rst_n),.cclk(cclk),.led(led),
        .spi_miso(spi_miso),.spi_ss(1'b1),.spi_mosi(1'b0),.spi_sck(1'b0),
        .spi_channel(spi_channel),.avr_tx(avr_tx),.avr_rx(avr_rx),.avr_rx_busy(avr_rx_busy));
    reg [7:0] received [0:511];
    integer receive_count=0, acknowledgements=0, response_count=0, consumed=0;
    integer capture_file;
    reg [7:0] incoming;
    integer bit_index, delay_index;
    // Independent center-of-bit decoder rather than another vendor RX module.
    initial forever begin
        @(negedge avr_rx);
        if(led[7]===1'b1) begin
            repeat(150) @(negedge clk);
            for(bit_index=0;bit_index<8;bit_index=bit_index+1) begin
                if(avr_rx!==1'b0 && avr_rx!==1'b1) $fatal(1,"UART data not binary");
                incoming[bit_index]=avr_rx;
                repeat(100) @(negedge clk);
            end
            if(avr_rx!==1'b1) $fatal(1,"UART stop bit missing");
            if(receive_count>=512) $fatal(1,"UART receive buffer overflow");
            received[receive_count]=incoming; receive_count=receive_count+1;
            // No following start bit is permitted until the AVR toggle ACK.
            for(delay_index=0;delay_index<1500;delay_index=delay_index+1) begin
                @(negedge clk);
                if(led[7]===1'b1 && avr_rx!==1'b1) $fatal(1,"UART transmitted while AVR ACK withheld");
            end
            if(ack_enabled) begin
                avr_rx_busy=~avr_rx_busy; acknowledgements=acknowledgements+1;
            end
        end
    end
    function automatic [15:0] crc_byte(input [15:0] previous, input [7:0] data);
        reg [15:0] value;
        integer i;
        begin
            value=previous^{data,8'b0};
            for(i=0;i<8;i=i+1) value=value[15] ? (value<<1)^16'h1021 : value<<1;
            crc_byte=value;
        end
    endfunction
    task automatic send_byte(input [7:0] value);
        integer i;
        begin
            avr_tx=0; repeat(100) @(negedge clk);
            for(i=0;i<8;i=i+1) begin avr_tx=value[i]; repeat(100) @(negedge clk); end
            avr_tx=1; repeat(100) @(negedge clk);
        end
    endtask
    reg [7:0] payload[0:65];
    reg [15:0] vector_crc;
    integer k;
    task automatic request(input [7:0] cmd, seq, length, input bit bad_crc);
        reg [15:0] crc;
        integer i;
        begin
            send_byte(8'ha5); send_byte(cmd); crc=crc_byte(16'hffff,cmd);
            send_byte(seq); crc=crc_byte(crc,seq); send_byte(length); crc=crc_byte(crc,length);
            for(i=0;i<length;i=i+1) begin send_byte(payload[i]); crc=crc_byte(crc,payload[i]); end
            crc=crc^(bad_crc ? 16'h1 : 16'h0);
            send_byte(crc[7:0]); send_byte(crc[15:8]);
        end
    endtask
    task automatic response(input [7:0] cmd, seq, length, input [119:0] body);
        reg [7:0] expected [0:20];
        reg [15:0] crc;
        integer i, timeout;
        begin
            expected[0]=8'h5a; expected[1]=cmd|8'h80; expected[2]=seq; expected[3]=length;
            crc=crc_byte(16'hffff,expected[1]); crc=crc_byte(crc,seq); crc=crc_byte(crc,length);
            for(i=0;i<length;i=i+1) begin expected[4+i]=body[i*8 +:8]; crc=crc_byte(crc,expected[4+i]); end
            expected[4+length]=crc[7:0]; expected[5+length]=crc[15:8]; timeout=0;
            while(receive_count<consumed+length+6 && timeout<100000) begin
                @(negedge clk); timeout=timeout+1;
            end
            if(receive_count!=consumed+length+6) $fatal(1,"UART response timeout/extra bytes cmd=%h count=%0d",cmd,receive_count-consumed);
            for(i=0;i<length+6;i=i+1)
                if(received[consumed+i]!==expected[i])
                    $fatal(1,"UART mismatch cmd=%h seq=%h byte=%0d expected=%h actual=%h",cmd,seq,i,expected[i],received[consumed+i]);
            for(i=0;i<length+6;i=i+1) $fwrite(capture_file,"%02x",received[consumed+i]);
            $fwrite(capture_file,"\n");
            consumed=receive_count; response_count=response_count+1;
        end
    endtask
    initial begin
        capture_file=$fopen("sim/uart-replies.hex","w");
        if(capture_file==0) $fatal(1,"cannot open UART response capture");
        repeat(10) @(negedge clk); rst_n=1;
        repeat(1100) begin
            @(negedge clk);
            if(avr_rx!==1'bz || spi_miso!==1'bz || spi_channel!==4'bzzzz)
                $fatal(1,"AVR pins must remain high impedance before cclk startup");
        end
        cclk=1; repeat(1300) @(negedge clk);
        if(led[7]!==1 || avr_rx!==1) $fatal(1,"AVR startup not ready");
        vector_crc=16'hffff;
        for(k=0;k<9;k=k+1) vector_crc=crc_byte(vector_crc,8'h31+k);
        if(vector_crc!==16'h29b1) $fatal(1,"CRC16 standard check vector failed");
        request(1,8'h21,0,0); response(1,8'h21,15,{8'd1,8'd10,16'd0,16'd0,16'd16384,16'd128,16'd128,8'd0,8'd4,8'd0});
        payload[0]=0;payload[1]=0;
        for(k=0;k<64;k=k+1) payload[k+2]=k;
        request(3,8'h22,66,0); response(3,8'h22,3,{80'd0,16'd32,8'd0});
        payload[0]=4;
        request(4,8'h23,1,0); response(4,8'h23,1,120'd5);
        payload[0]=32;payload[1]=0;payload[2]=255;payload[3]=0;
        request(3,8'h24,4,1); response(3,8'h24,1,120'd1);
        request(1,8'h25,0,0); response(1,8'h25,15,{8'd1,8'd10,16'd0,16'd0,16'd16384,16'd128,16'd128,8'd0,8'd4,8'd0});
        payload[0]=33;payload[1]=0;payload[2]=255;payload[3]=0;
        request(3,8'h26,4,0); response(3,8'h26,1,120'd4);
        payload[0]=0;payload[1]=0;payload[2]=0;for(k=0;k<60;k=k+1)payload[k+3]=k;
        request(6,8'h27,63,0); response(6,8'h27,5,120'h0000000500);
        payload[0]=0;payload[1]=0;payload[2]=1;for(k=0;k<12;k=k+1)payload[k+3]=0;
        request(6,8'h31,15,0);response(6,8'h31,1,120'd4); // gap at node256 must not truncate to0
        payload[0]=1;payload[1]=0;payload[2]=0;for(k=0;k<63;k=k+1)payload[k+3]=k;
        request(6,8'h32,66,0);response(6,8'h32,5,120'h003f000500);
        request(2,8'h28,0,0); response(2,8'h28,1,120'd0);
        payload[0]=0;payload[1]=0;payload[2]=1;
        request(5,8'h29,3,0); response(5,8'h29,1,120'd7);
        request(1,8'h2a,0,0); response(1,8'h2a,15,{8'd1,8'd10,16'd63,16'd5,16'd16384,16'd128,16'd128,8'd0,8'd4,8'd0});
        request(7,8'h2b,0,0); response(7,8'h2b,1,120'd0);
        repeat(1700) @(negedge clk);
        if(receive_count!=consumed || acknowledgements!=receive_count)
            $fatal(1,"Unexpected UART bytes or missing acknowledgements");
        // Always check interrupted-transfer recovery with a withheld AVR ACK.
        ack_enabled=0;
        request(1,8'h60,0,0);
        while(receive_count==consumed) @(negedge clk);
        rst_n=0;cclk=0;repeat(20)@(negedge clk);
        rst_n=1;cclk=1;repeat(1800)@(negedge clk);
        consumed=receive_count;ack_enabled=1;
        request(1,8'h61,0,0); response(1,8'h61,15,{8'd1,8'd10,16'd0,16'd0,16'd16384,16'd128,16'd128,8'd0,8'd4,8'd0});
        repeat(1700)@(negedge clk);
        if(acknowledgements!=receive_count-1) $fatal(1,"ACK count differs from deliberate one-byte abort");
        $display("PASS interrupted_reset_recovery=1 max_depth_packet64bytes=1 model5nodes=1 incomplete_run_guard=1 crc16_rejection=1 begin_invalidates=1");
        $display("PASS tree_uart_responses=%0d bytes=%0d delayed_acknowledgements=%0d startup_tristate=1 baud=500000 ack_delay_cycles=1500",response_count,receive_count,acknowledgements);
        $fclose(capture_file);
        $finish;
    end
    initial begin repeat(2000000) @(posedge clk); $fatal(1,"UART test watchdog"); end
endmodule
