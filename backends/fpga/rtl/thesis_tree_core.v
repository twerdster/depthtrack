// Resident-image, tagged/interleaved integer-profile depth-tree inference.
// See INTERFACE.md. Verilog2001; RAMs have ordinary synchronous memory ports.
module thesis_tree_core #(
    parameter WIDTH=128,parameter HEIGHT=128,parameter ADDR_WIDTH=14,
    parameter MAX_LEVELS=10,parameter CONTEXTS=4
) (
    input wire clk,input wire reset,
    input wire frame_we,input wire [ADDR_WIDTH-1:0] frame_addr,input wire [15:0] frame_data,
    input wire model_clear,input wire model_we,input wire model_is_leaf,input wire [9:0] model_addr,input wire [95:0] model_data,
    input wire start,input wire [3:0] levels,
    output reg busy,output reg done,output reg error,
    output reg [31:0] cycles,output reg [31:0] valid_pixels,
    output reg [10:0] model_node_count,output reg [10:0] model_leaf_count,
    input wire [ADDR_WIDTH-1:0] label_addr,output wire [7:0] label_data
);
    localparam PIXELS=WIDTH*HEIGHT;
    localparam FREE=0,WAIT_RECIP=1,READY=2,IN_FLIGHT=3,COMPLETE=4;
    // Context tags use4bits; supported CONTEXTS=1..16.
    reg [2:0] ctx_state[0:CONTEXTS-1];
    reg [15:0] ctx_x[0:CONTEXTS-1],ctx_y[0:CONTEXTS-1];
    reg [15:0] ctx_z[0:CONTEXTS-1],ctx_recip[0:CONTEXTS-1];
    reg [ADDR_WIDTH-1:0] ctx_pixel[0:CONTEXTS-1];
    reg [9:0] ctx_node[0:CONTEXTS-1],ctx_path[0:CONTEXTS-1];
    reg [3:0] ctx_depth[0:CONTEXTS-1];
    reg [7:0] ctx_label[0:CONTEXTS-1];
    (* ram_style="block" *) reg [95:0] node_ram[0:1023];
    (* ram_style="block" *) reg [7:0] leaf_ram[0:1023];
    wire node_host_write=model_we && !model_clear && !busy && model_addr<1023 && {1'b0,model_addr}<=model_node_count;
    wire leaf_host_write=model_we && !model_clear && !busy && model_is_leaf && {1'b0,model_addr}<=model_leaf_count;
    wire node_write=node_host_write && !model_is_leaf;
    reg result_complete;
    reg [3:0] run_levels;
    wire [10:0] required_leaves=11'd1<<levels;
    wire [10:0] required_nodes=required_leaves-11'd1;
    reg [ADDR_WIDTH:0] admitted,finished;
    reg [15:0] next_x,next_y;
    reg centre_pending;
    reg reciprocal_start;
    reg [3:0] reciprocal_tag;
    reg [15:0] reciprocal_z;
    wire reciprocal_busy,reciprocal_done;
    wire math_reset;
    wire [15:0] reciprocal_value;
    thesis_reciprocal reciprocal_unit(clk,math_reset,reciprocal_start,reciprocal_z,
                                      reciprocal_busy,reciprocal_done,reciprocal_value);
    integer i;
    reg found_free,found_ready,found_complete;
    reg [3:0] free_tag,ready_tag,complete_tag;
    // Round-robin READY scan avoids starvation of older pixel traversals.
    reg [3:0] rr;
    integer scan,index;
    always @* begin
        found_free=0;found_ready=0;found_complete=0;
        free_tag=0;ready_tag=0;complete_tag=0;
        for(scan=0;scan<CONTEXTS;scan=scan+1) begin
            if(!found_free && ctx_state[scan]==FREE) begin found_free=1;free_tag=scan;end
            if(!found_complete && ctx_state[scan]==COMPLETE) begin found_complete=1;complete_tag=scan;end
            index=rr+scan;
            if(index>=CONTEXTS) index=index-CONTEXTS;
            if(!found_ready && ctx_state[index]==READY) begin found_ready=1;ready_tag=index;end
        end
    end
    wire fetch_bad=found_ready && {1'b0,ctx_node[ready_tag]}>=model_node_count;
    wire node_fetch=busy && found_ready && !fetch_bad;
    reg node_read_valid;
    reg [3:0] node_read_tag;
    reg [95:0] issue_node;
    // The host write port and registered inference read port infer model BRAM.
    always @(posedge clk) begin
        if(node_write) node_ram[model_addr]<=model_data;
        if(node_fetch) issue_node<=node_ram[ctx_node[ready_tag]];
    end
    wire node_bad=!issue_node[85] ||
                  (issue_node[83] && issue_node[84]) || |issue_node[95:86];
    wire issue=busy && node_read_valid && !node_bad;
    wire signed [17:0] row1,col1,row2,col2;
    wire [3:0] coord_valid;
    thesis_coord_pipe c0(clk,math_reset,issue,ctx_y[node_read_tag],issue_node[15:0],ctx_z[node_read_tag],ctx_recip[node_read_tag],coord_valid[0],row1);
    thesis_coord_pipe c1(clk,math_reset,issue,ctx_x[node_read_tag],issue_node[31:16],ctx_z[node_read_tag],ctx_recip[node_read_tag],coord_valid[1],col1);
    thesis_coord_pipe c2(clk,math_reset,issue,ctx_y[node_read_tag],issue_node[47:32],ctx_z[node_read_tag],ctx_recip[node_read_tag],coord_valid[2],row2);
    thesis_coord_pipe c3(clk,math_reset,issue,ctx_x[node_read_tag],issue_node[63:48],ctx_z[node_read_tag],ctx_recip[node_read_tag],coord_valid[3],col2);
    reg [4:0] pipe_valid;
    reg [3:0] pipe_tag[0:4];
    reg [4:0] pipe_flags[0:4];
    reg signed [15:0] pipe_cut[0:4];
    wire neighbours=busy && pipe_valid[4];
    wire outside1=row1<0 || row1>=HEIGHT || col1<0 || col1>=WIDTH;
    wire outside2=row2<0 || row2>=HEIGHT || col2<0 || col2>=WIDTH;
    wire [ADDR_WIDTH-1:0] address1=outside1 ? 0 : row1*WIDTH+col1;
    wire [ADDR_WIDTH-1:0] address2=outside2 ? 0 : row2*WIDTH+col2;
    wire centre_issue=busy && !neighbours && !centre_pending && !reciprocal_busy &&
                      !reciprocal_start && admitted<PIXELS && found_free;
    wire host_frame_write=frame_we && !busy && frame_addr<PIXELS;
    wire [ADDR_WIDTH-1:0] read_a=host_frame_write ? frame_addr : neighbours ? address1 : admitted[ADDR_WIDTH-1:0];
    wire [ADDR_WIDTH-1:0] read_b=neighbours ? address2 : 0;
    (* ram_style="block" *) reg [15:0] frame_ram[0:PIXELS-1];
    reg [15:0] data_a,data_b;
    always @(posedge clk) begin
        if(host_frame_write) frame_ram[read_a]<=frame_data;
        data_a<=frame_ram[read_a];
        data_b<=frame_ram[read_b];
    end
    reg memory_valid,memory_centre,memory_outside1,memory_outside2;
    reg [3:0] memory_tag;
    reg [4:0] memory_flags;
    reg signed [15:0] memory_cut;
    wire [15:0] sample1=memory_outside1 ? 0 : data_a;
    wire [15:0] sample2=memory_outside2 ? 0 : data_b;
    wire signed [16:0] difference=$signed({1'b0,sample1})-$signed({1'b0,sample2});
    wire branch=sample1==0 && sample2==0 ? memory_flags[0] :
                sample1==0 ? memory_flags[1] : sample2==0 ? memory_flags[2] :
                memory_flags[3] ? 1'b0 : memory_flags[4] ? 1'b1 : difference>$signed({memory_cut[15],memory_cut});
    wire [9:0] next_path={ctx_path[memory_tag][8:0],branch};
    wire is_last=ctx_depth[memory_tag]+1'b1==run_levels;
    wire leaf_read=busy && memory_valid && !memory_centre && is_last;
    wire commit_bad=leaf_read && {1'b0,next_path}>=model_leaf_count;
    reg leaf_read_valid;
    reg [3:0] leaf_read_tag;
    reg [7:0] leaf_read_data;
    always @(posedge clk) begin
        if(leaf_host_write) leaf_ram[model_addr]<=model_data[7:0];
        if(leaf_read) leaf_read_data<=leaf_ram[next_path];
    end
    wire abort_run=busy && (fetch_bad || (node_read_valid && node_bad) || commit_bad);
    assign math_reset=reset || abort_run || (!busy && start && !frame_we && !model_we && !model_clear);
    wire write_label=busy && found_complete && !abort_run;
    (* ram_style="block" *) reg [7:0] label_ram[0:PIXELS-1];
    reg [7:0] label_q;
    always @(posedge clk) begin
        if(write_label) label_ram[ctx_pixel[complete_tag]]<=ctx_label[complete_tag];
        label_q<=label_ram[label_addr];
    end
    assign label_data=!reset && result_complete && !busy && label_addr<PIXELS ? label_q : 8'd0;
    always @(posedge clk) begin
        if(reset) begin
            busy<=0;done<=0;error<=0;cycles<=0;valid_pixels<=0;
            model_node_count<=0;model_leaf_count<=0;result_complete<=0;run_levels<=0;
            node_read_valid<=0;node_read_tag<=0;leaf_read_valid<=0;leaf_read_tag<=0;
            admitted<=0;finished<=0;next_x<=0;next_y<=0;rr<=0;
            centre_pending<=0;reciprocal_start<=0;reciprocal_tag<=0;reciprocal_z<=0;
            pipe_valid<=0;memory_valid<=0;memory_centre<=0;
            memory_outside1<=0;memory_outside2<=0;memory_tag<=0;memory_flags<=0;memory_cut<=0;
            for(i=0;i<CONTEXTS;i=i+1) begin
                ctx_state[i]<=FREE;ctx_x[i]<=0;ctx_y[i]<=0;ctx_z[i]<=0;ctx_recip[i]<=0;
                ctx_pixel[i]<=0;ctx_node[i]<=0;ctx_path[i]<=0;ctx_depth[i]<=0;ctx_label[i]<=0;
            end
            for(i=0;i<5;i=i+1) begin pipe_tag[i]<=0;pipe_flags[i]<=0;pipe_cut[i]<=0;end
        end else begin
            done<=0;reciprocal_start<=0;
            node_read_valid<=node_fetch;
            if(node_fetch) node_read_tag<=ready_tag;
            leaf_read_valid<=leaf_read && !abort_run;
            if(leaf_read) leaf_read_tag<=memory_tag;
            pipe_valid<={pipe_valid[3:0],issue};
            pipe_tag[0]<=node_read_tag;pipe_flags[0]<=issue_node[84:80];pipe_cut[0]<=issue_node[79:64];
            for(i=1;i<5;i=i+1) begin
                pipe_tag[i]<=pipe_tag[i-1];pipe_flags[i]<=pipe_flags[i-1];pipe_cut[i]<=pipe_cut[i-1];
            end
            memory_valid<=busy && (neighbours || centre_issue);
            memory_centre<=centre_issue;
            if(neighbours) begin
                memory_tag<=pipe_tag[4];memory_flags<=pipe_flags[4];memory_cut<=pipe_cut[4];
                memory_outside1<=outside1;memory_outside2<=outside2;
            end else if(centre_issue) memory_tag<=free_tag;
            if(!busy) begin
                if(frame_we || model_we || model_clear) result_complete<=0;
                if(model_clear) begin model_node_count<=0;model_leaf_count<=0;end
                else begin
                    if(node_write && {1'b0,model_addr}==model_node_count) model_node_count<=model_node_count+1'b1;
                    if(leaf_host_write && {1'b0,model_addr}==model_leaf_count) model_leaf_count<=model_leaf_count+1'b1;
                end
                if(start && !frame_we && !model_we && !model_clear) begin
                    result_complete<=0;error<=0;cycles<=0;valid_pixels<=0;
                    admitted<=0;finished<=0;next_x<=0;next_y<=0;rr<=0;
                    centre_pending<=0;pipe_valid<=0;memory_valid<=0;node_read_valid<=0;leaf_read_valid<=0;
                    for(i=0;i<CONTEXTS;i=i+1) ctx_state[i]<=FREE;
                    if(levels==0 || levels>MAX_LEVELS || levels>10 || model_node_count<required_nodes || model_leaf_count<required_leaves) begin done<=1;error<=1;end
                    else begin busy<=1;run_levels<=levels;end
                end
            end else begin
                cycles<=cycles+1'b1;
                if(abort_run) begin
                    busy<=0;done<=1;error<=1;result_complete<=0;
                    pipe_valid<=0;memory_valid<=0;centre_pending<=0;node_read_valid<=0;leaf_read_valid<=0;
                    for(i=0;i<CONTEXTS;i=i+1) ctx_state[i]<=FREE;
                end else begin
                    if(node_fetch) begin
                        ctx_state[ready_tag]<=IN_FLIGHT;
                        rr<=ready_tag==CONTEXTS-1 ? 0 : ready_tag+1'b1;
                    end
                    if(centre_issue) begin
                        centre_pending<=1;ctx_state[free_tag]<=WAIT_RECIP;
                        ctx_x[free_tag]<=next_x;ctx_y[free_tag]<=next_y;
                        ctx_pixel[free_tag]<=admitted[ADDR_WIDTH-1:0];
                        ctx_node[free_tag]<=0;ctx_path[free_tag]<=0;ctx_depth[free_tag]<=0;
                        admitted<=admitted+1'b1;
                        if(next_x==WIDTH-1) begin next_x<=0;next_y<=next_y+1'b1;end
                        else next_x<=next_x+1'b1;
                    end
                    if(memory_valid && memory_centre) begin
                        centre_pending<=0;ctx_z[memory_tag]<=data_a;
                        if(data_a==0) begin ctx_label[memory_tag]<=7;ctx_state[memory_tag]<=COMPLETE;end
                        else begin
                            valid_pixels<=valid_pixels+1'b1;
                            reciprocal_tag<=memory_tag;reciprocal_z<=data_a;reciprocal_start<=1;
                        end
                    end
                    if(reciprocal_done) begin ctx_recip[reciprocal_tag]<=reciprocal_value;ctx_state[reciprocal_tag]<=READY;end
                    if(memory_valid && !memory_centre) begin
                        if(!is_last) begin
                            ctx_node[memory_tag]<={ctx_node[memory_tag][8:0],1'b0}+10'd1+branch;
                            ctx_path[memory_tag]<=next_path;ctx_depth[memory_tag]<=ctx_depth[memory_tag]+1'b1;
                            ctx_state[memory_tag]<=READY;
                        end
                    end
                    if(leaf_read_valid) begin ctx_label[leaf_read_tag]<=leaf_read_data;ctx_state[leaf_read_tag]<=COMPLETE;end
                    if(write_label) begin
                        ctx_state[complete_tag]<=FREE;finished<=finished+1'b1;
                        if(finished==PIXELS-1) begin busy<=0;done<=1;result_complete<=1;end
                    end
                end
            end
        end
    end
endmodule
