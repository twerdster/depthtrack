`timescale 1ns/1ps
module tb_thesis_controls;
    parameter CONTEXTS=4;
    localparam WIDTH=8,HEIGHT=8,ADDR_WIDTH=6,PIXELS=64;
    reg clk=0;always #10 clk=~clk;
    reg reset=1,frame_we=0,model_clear=0,model_we=0,model_is_leaf=0,start=0;
    reg [ADDR_WIDTH-1:0] frame_addr=0,label_addr=0;
    reg [15:0] frame_data=0;reg [9:0] model_addr=0;reg [95:0] model_data=0;
    reg [3:0] levels=10;wire busy,done,error;wire [31:0] cycles,valid_pixels;wire [7:0] label_data;
    wire [10:0] model_node_count,model_leaf_count;
    thesis_tree_core #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.ADDR_WIDTH(ADDR_WIDTH),.CONTEXTS(CONTEXTS)) dut(.*);
    integer i,watchdog,cases=0;
    task tick;begin @(posedge clk);#1;end endtask
    task run_start;input [3:0] lev;begin @(negedge clk);levels=lev;start=1;tick();@(negedge clk);start=0;end endtask
    task wait_done;input want_error;begin
        watchdog=0;
        while(!done)begin tick();watchdog=watchdog+1;if(watchdog>20000)$fatal(1,"timeout");end
        if(error!==want_error)$fatal(1,"error expected %d got %d",want_error,error);
        cases=cases+1;
    end endtask
    task write_model;input leaf;input [9:0] addr;input [95:0] value;begin
        @(negedge clk);model_we=1;model_is_leaf=leaf;model_addr=addr;model_data=value;tick();
        @(negedge clk);model_we=0;
    end endtask
    task load_model;input omit_last_leaf;begin
        for(i=0;i<1023;i=i+1)write_model(0,i,96'h003700000000000000000000);
        for(i=0;i<(omit_last_leaf ? 1023 : 1024);i=i+1)write_model(1,i,i==1023 ? 8'd255 : 8'd5);
    end endtask
    task check_labels;input [7:0] want;begin
        for(i=0;i<PIXELS;i=i+1)begin @(negedge clk);label_addr=i;tick();if(label_data!==(i==0 ? 8'd7 : want))$fatal(1,"labels %d got %d",i,label_data);end
        if(valid_pixels!==63)$fatal(1,"valid pixels");
    end endtask
    initial begin
        repeat(3)tick();@(negedge clk);reset=0;
        for(i=0;i<PIXELS;i=i+1)begin @(negedge clk);frame_we=1;frame_addr=i;frame_data=i==0 ? 0 : 1000;tick();end
        @(negedge clk);frame_we=0;
        run_start(0);wait_done(1);if(busy || cycles!=0)$fatal(1,"invalid levels");
        write_model(0,1,96'h003700000000000000000000);write_model(1,3,5);
        if(model_node_count!=0 || model_leaf_count!=0)$fatal(1,"gap writes accepted");cases=cases+1;
        run_start(10);wait_done(1);if(cycles!=0)$fatal(1,"incomplete model accepted");
        load_model(1);run_start(10);wait_done(1);
        if(model_node_count!=1023 || model_leaf_count!=1023)$fatal(1,"prefix counts");
        write_model(1,1023,255);write_model(0,1023,96'h003700000000000000000000);
        if(model_node_count!=1023 || model_leaf_count!=1024)$fatal(1,"full counts");
        run_start(10);repeat(30)tick();
        @(negedge clk);frame_we=1;frame_addr=1;frame_data=0;model_we=1;model_is_leaf=1;model_addr=1023;model_data=99;model_clear=1;start=1;
        repeat(2)tick();@(negedge clk);frame_we=0;model_we=0;model_clear=0;start=0;
        wait_done(0);check_labels(255);
        if(model_node_count!=1023 || model_leaf_count!=1024)$fatal(1,"active clear changed counts");
        write_model(1,1023,6);run_start(10);wait_done(0);check_labels(6);
        write_model(0,1022,0);run_start(10);wait_done(1);
        write_model(0,1022,96'h003700000000000000000000);run_start(10);wait_done(0);check_labels(6);
        @(negedge clk);model_clear=1;model_we=1;model_is_leaf=0;model_addr=0;model_data=96'h003700000000000000000000;tick();
        @(negedge clk);model_clear=0;model_we=0;
        if(model_node_count || model_leaf_count || label_data!==0)$fatal(1,"clear priority/count/output");cases=cases+1;
        run_start(10);wait_done(1);
        load_model(0);run_start(10);repeat(10)tick();@(negedge clk);reset=1;tick();@(negedge clk);reset=0;
        if(model_node_count || model_leaf_count)$fatal(1,"reset presence");
        load_model(0);run_start(10);wait_done(0);check_labels(255);
        $display("PASS core control cases=%0d",cases);$finish;
    end
endmodule
