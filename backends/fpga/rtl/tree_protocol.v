// Thesis depth-tree transport v4.0. CRC16 framed stop/wait byte stream.
// A packet is buffered and CRC-checked before any memory or RUN side effect.
module tree_protocol #(
    parameter WIDTH=128, HEIGHT=128, ADDR_WIDTH=14, MAX_LEVELS=10,
    parameter CONTEXTS=4, TIMEOUT_CYCLES=5000000
)(
    input clk, reset,
    input [7:0] rx_data, input rx_valid, output rx_ready,
    output [7:0] tx_data, output tx_valid, input tx_ready,
    output reg [15:0] loaded_count
);
    localparam PIXELS=WIDTH*HEIGHT, MAX_NODES=(1<<MAX_LEVELS)-1, MAX_LEAVES=(1<<MAX_LEVELS);
    localparam S_SYNC=0,S_CMD=1,S_SEQ=2,S_LEN=3,S_PAY=4,S_CRC_LO=5,S_CRC_HI=6,
        S_DISPATCH=7,S_DEPTH=8,S_DEPTH_DRAIN=9,S_MODEL_ACCUM=10,S_MODEL_WRITE=11,
        S_MODEL_DRAIN=12,S_RUN=13,S_READ_WAIT=14,S_READ_CAPTURE=15,
        S_REPLY_START=16,S_REPLY_CRC=17,S_TX=18;
    reg [4:0] state;
    reg [7:0] command,sequence_id,request_length,request_index;
    reg [7:0] request_payload[0:65];
    reg [15:0] request_crc; reg [7:0] request_crc_low;
    reg [31:0] timeout_count;
    reg result_valid;
    reg [15:0] node_count,leaf_count;
    reg [7:0] reply_payload[0:66];
    reg [6:0] reply_length,crc_index,tx_index;
    reg [15:0] reply_crc; reg [7:0] tx_byte; reg tx_pending;
    reg [15:0] transfer_address;
    reg [6:0] transfer_count,transfer_index,payload_offset;
    reg [3:0] model_byte;
    wire [15:0] argument_address={request_payload[1],request_payload[0]};
    wire [16:0] depth_words=({9'b0,request_length}-17'd2)>>1;
    wire [16:0] depth_end={1'b0,argument_address}+depth_words;
    wire [16:0] read_end={1'b0,argument_address}+{9'b0,request_payload[2]};
    // Only these five node packet lengths are legal. No general divider,
    // modulus, or variable record-index multiplier is synthesized here.
    function [2:0] packet_nodes;
        input [7:0] length;
        begin
            case(length)
                8'd15:packet_nodes=1;8'd27:packet_nodes=2;
                8'd39:packet_nodes=3;8'd51:packet_nodes=4;
                8'd63:packet_nodes=5;default:packet_nodes=0;
            endcase
        end
    endfunction
    wire [2:0] model_records=packet_nodes(request_length);
    wire [15:0] model_argument_address={request_payload[2],request_payload[1]};
    wire [16:0] node_end={1'b0,model_argument_address}+{14'b0,model_records};
    wire [16:0] leaf_end={1'b0,model_argument_address}+{9'b0,request_length}-17'd3;
    wire [15:0] required_leaves=16'd1 << request_payload[0][3:0];
    wire [15:0] required_nodes=required_leaves-1'b1;
    reg frame_we,model_we,model_is_leaf,core_start,model_clear;
    reg [ADDR_WIDTH-1:0] frame_addr,label_addr;
    reg [15:0] frame_data;
    reg [9:0] model_addr;
    reg [95:0] model_data;
    reg [3:0] core_levels;
    wire [7:0] label_data;
    wire core_busy,core_done,core_error;
    wire [31:0] core_cycles,core_valid_pixels;
    thesis_tree_core #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.ADDR_WIDTH(ADDR_WIDTH),
        .MAX_LEVELS(MAX_LEVELS),.CONTEXTS(CONTEXTS)) classifier (
        .clk(clk),.reset(reset),.frame_we(frame_we),.frame_addr(frame_addr),.frame_data(frame_data),
        .model_clear(model_clear),.model_we(model_we),.model_is_leaf(model_is_leaf),.model_addr(model_addr),.model_data(model_data),
        .start(core_start),.levels(core_levels),.busy(core_busy),.done(core_done),.error(core_error),
        .cycles(core_cycles),.valid_pixels(core_valid_pixels),.label_addr(label_addr),.label_data(label_data));
    assign rx_ready=(state<=S_CRC_HI)&&!reset;
    assign tx_data=tx_byte;
    assign tx_valid=tx_pending&&!reset;
    function [15:0] crc_byte;
        input [15:0] initial_crc; input [7:0] data;
        reg [15:0] c; integer j;
        begin
            c=initial_crc^{data,8'b0};
            for(j=0;j<8;j=j+1) if(c[15]) c=(c<<1)^16'h1021;else c=c<<1;
            crc_byte=c;
        end
    endfunction
    task status_reply;
        input [7:0] status;
        begin reply_payload[0]<=status;reply_length<=1;state<=S_REPLY_START;end
    endtask
    always @(posedge clk) begin
        if(reset) begin
            state<=S_SYNC;command<=0;sequence_id<=0;request_length<=0;request_index<=0;
            request_crc<=16'hffff;request_crc_low<=0;timeout_count<=0;
            result_valid<=0;loaded_count<=0;node_count<=0;leaf_count<=0;
            reply_length<=0;crc_index<=0;reply_crc<=16'hffff;tx_index<=0;tx_byte<=0;tx_pending<=0;
            transfer_address<=0;transfer_count<=0;transfer_index<=0;payload_offset<=0;model_byte<=0;
            frame_we<=0;frame_addr<=0;frame_data<=0;model_we<=0;model_is_leaf<=0;
            model_addr<=0;model_data<=0;core_start<=0;model_clear<=0;core_levels<=10;label_addr<=0;
        end else begin
            frame_we<=0;model_we<=0;core_start<=0;model_clear<=0;
            if(state>=S_CMD&&state<=S_CRC_HI&&!(rx_valid&&rx_ready)) begin
                if(timeout_count>=TIMEOUT_CYCLES-1) begin state<=S_SYNC;timeout_count<=0;end
                else timeout_count<=timeout_count+1'b1;
            end else begin
                timeout_count<=0;
                case(state)
                S_SYNC: if(rx_valid&&rx_ready&&rx_data==8'ha5) state<=S_CMD;
                S_CMD: if(rx_valid&&rx_ready) begin command<=rx_data;request_crc<=crc_byte(16'hffff,rx_data);state<=S_SEQ;end
                S_SEQ: if(rx_valid&&rx_ready) begin sequence_id<=rx_data;request_crc<=crc_byte(request_crc,rx_data);state<=S_LEN;end
                S_LEN: if(rx_valid&&rx_ready) begin
                    request_length<=rx_data;request_index<=0;request_crc<=crc_byte(request_crc,rx_data);
                    if(rx_data==0) state<=S_CRC_LO;else state<=S_PAY;
                end
                S_PAY: if(rx_valid&&rx_ready) begin
                    if(request_index<66) request_payload[request_index]<=rx_data;
                    request_crc<=crc_byte(request_crc,rx_data);
                    if(request_index==request_length-1'b1) state<=S_CRC_LO;else request_index<=request_index+1'b1;
                end
                S_CRC_LO: if(rx_valid&&rx_ready) begin request_crc_low<=rx_data;state<=S_CRC_HI;end
                S_CRC_HI: if(rx_valid&&rx_ready) begin
                    if({rx_data,request_crc_low}!=request_crc) status_reply(1);
                    else if(request_length>66) status_reply(3);else state<=S_DISPATCH;
                end
                S_DISPATCH: case(command)
                8'h01: if(request_length!=0) status_reply(3);else begin
                    reply_payload[0]<=0;reply_payload[1]<=4;reply_payload[2]<=0;
                    reply_payload[3]<=WIDTH%256;reply_payload[4]<=WIDTH/256;
                    reply_payload[5]<=HEIGHT%256;reply_payload[6]<=HEIGHT/256;
                    reply_payload[7]<=PIXELS%256;reply_payload[8]<=PIXELS/256;
                    reply_payload[9]<=node_count[7:0];reply_payload[10]<=node_count[15:8];
                    reply_payload[11]<=leaf_count[7:0];reply_payload[12]<=leaf_count[15:8];
                    reply_payload[13]<=MAX_LEVELS;reply_payload[14]<=1;
                    reply_length<=15;state<=S_REPLY_START;
                end
                8'h02: if(request_length!=0) status_reply(3);else begin loaded_count<=0;result_valid<=0;status_reply(0);end
                8'h03: if(request_length<4||request_length>66||request_length[0]) status_reply(3);
                else if(depth_end>PIXELS||!((argument_address==loaded_count)||
                    ((argument_address<loaded_count)&&(depth_end<=loaded_count)))) status_reply(4);
                else begin
                    result_valid<=0;transfer_address<=argument_address;transfer_count<=depth_words[6:0];transfer_index<=0;payload_offset<=2;
                    if(argument_address==loaded_count) loaded_count<=depth_end[15:0];
                    state<=S_DEPTH;
                end
                8'h04: if(request_length!=1) status_reply(3);
                else if(request_payload[0]<1||request_payload[0]>MAX_LEVELS) status_reply(3);
                else if(loaded_count!=PIXELS) status_reply(5);
                else if(node_count<required_nodes||leaf_count<required_leaves) status_reply(8);
                else begin result_valid<=0;core_levels<=request_payload[0][3:0];core_start<=1;state<=S_RUN;end
                8'h05: if(request_length!=3) status_reply(3);
                else if(request_payload[2]==0||request_payload[2]>64) status_reply(3);
                else if(read_end>PIXELS) status_reply(4);
                else if(!result_valid) status_reply(7);
                else begin
                    transfer_address<=argument_address;transfer_count<=request_payload[2];transfer_index<=0;
                    reply_payload[0]<=0;reply_payload[1]<=argument_address[7:0];reply_payload[2]<=argument_address[15:8];
                    reply_length<=request_payload[2]+7'd3;label_addr<=argument_address[ADDR_WIDTH-1:0];state<=S_READ_WAIT;
                end
                8'h06: if(request_length<4||request_payload[0]>1) status_reply(3);
                else if(request_payload[0]==0&&model_records==0) status_reply(3);
                else if(request_payload[0]==0&&(node_end>MAX_NODES||!((model_argument_address==node_count)||
                    ((model_argument_address<node_count)&&(node_end<=node_count))))) status_reply(4);
                else if(request_payload[0]==1&&(leaf_end>MAX_LEAVES||!((model_argument_address==leaf_count)||
                    ((model_argument_address<leaf_count)&&(leaf_end<=leaf_count))))) status_reply(4);
                else begin
                    result_valid<=0;model_is_leaf<=request_payload[0][0];transfer_address<=model_argument_address;
                    transfer_count<=request_payload[0]?request_length-3:{4'b0,model_records};transfer_index<=0;payload_offset<=3;model_byte<=0;model_data<=0;
                    if(request_payload[0]==0&&model_argument_address==node_count) node_count<=node_end[15:0];
                    if(request_payload[0]==1&&model_argument_address==leaf_count) leaf_count<=leaf_end[15:0];
                    state<=S_MODEL_ACCUM;
                end
                8'h07: if(request_length!=0) status_reply(3);else begin node_count<=0;leaf_count<=0;model_clear<=1;result_valid<=0;status_reply(0);end
                default: status_reply(2);
                endcase
                S_DEPTH: begin
                    frame_we<=1;frame_addr<=transfer_address[ADDR_WIDTH-1:0];
                    frame_data<={request_payload[payload_offset+7'd1],request_payload[payload_offset]};
                    payload_offset<=payload_offset+7'd2;
                    transfer_address<=transfer_address+1'b1;
                    if(transfer_index==transfer_count-1'b1) state<=S_DEPTH_DRAIN;else transfer_index<=transfer_index+1'b1;
                end
                S_DEPTH_DRAIN: begin
                    reply_payload[0]<=0;reply_payload[1]<=loaded_count[7:0];reply_payload[2]<=loaded_count[15:8];
                    reply_length<=3;state<=S_REPLY_START;
                end
                S_MODEL_ACCUM: begin
                    payload_offset<=payload_offset+1'b1;
                    if(model_is_leaf) begin model_data<={88'b0,request_payload[payload_offset]};state<=S_MODEL_WRITE;end
                    else begin
                        model_data[model_byte*8+:8]<=request_payload[payload_offset];
                        if(model_byte==11) state<=S_MODEL_WRITE;else model_byte<=model_byte+1'b1;
                    end
                end
                S_MODEL_WRITE: begin
                    model_we<=1;model_addr<=transfer_address[9:0];transfer_address<=transfer_address+1'b1;model_byte<=0;
                    if(transfer_index==transfer_count-1'b1) state<=S_MODEL_DRAIN;
                    else begin transfer_index<=transfer_index+1'b1;state<=S_MODEL_ACCUM;end
                end
                S_MODEL_DRAIN: begin
                    reply_payload[0]<=0;reply_payload[1]<=node_count[7:0];reply_payload[2]<=node_count[15:8];
                    reply_payload[3]<=leaf_count[7:0];reply_payload[4]<=leaf_count[15:8];
                    reply_length<=5;state<=S_REPLY_START;
                end
                S_RUN: if(core_done) begin
                    if(core_error) status_reply(6);else begin
                        result_valid<=1;reply_payload[0]<=0;
                        reply_payload[1]<=core_cycles[7:0];reply_payload[2]<=core_cycles[15:8];
                        reply_payload[3]<=core_cycles[23:16];reply_payload[4]<=core_cycles[31:24];
                        reply_payload[5]<=core_valid_pixels[7:0];reply_payload[6]<=core_valid_pixels[15:8];
                        reply_payload[7]<=core_valid_pixels[23:16];reply_payload[8]<=core_valid_pixels[31:24];
                        reply_payload[9]<=0;reply_length<=10;state<=S_REPLY_START;
                    end
                end
                S_READ_WAIT: state<=S_READ_CAPTURE;
                S_READ_CAPTURE: begin
                    reply_payload[transfer_index+7'd3]<=label_data;
                    if(transfer_index==transfer_count-1'b1) state<=S_REPLY_START;
                    else begin transfer_index<=transfer_index+1'b1;transfer_address<=transfer_address+1'b1;
                        label_addr<=transfer_address+1'b1;state<=S_READ_WAIT;end
                end
                S_REPLY_START: begin reply_crc<=16'hffff;crc_index<=0;state<=S_REPLY_CRC;end
                S_REPLY_CRC: begin
                    if(crc_index==0) reply_crc<=crc_byte(reply_crc,command|8'h80);
                    else if(crc_index==1) reply_crc<=crc_byte(reply_crc,sequence_id);
                    else if(crc_index==2) reply_crc<=crc_byte(reply_crc,{1'b0,reply_length});
                    else reply_crc<=crc_byte(reply_crc,reply_payload[crc_index-7'd3]);
                    if(crc_index==reply_length+7'd2) begin tx_byte<=8'h5a;tx_pending<=1;tx_index<=0;state<=S_TX;end
                    else crc_index<=crc_index+1'b1;
                end
                S_TX: if(tx_pending&&tx_ready) begin
                    tx_index<=tx_index+1'b1;
                    if(tx_index==reply_length+7'd5) begin tx_pending<=0;state<=S_SYNC;end
                    else if(tx_index==0) tx_byte<=command|8'h80;
                    else if(tx_index==1) tx_byte<=sequence_id;
                    else if(tx_index==2) tx_byte<={1'b0,reply_length};
                    else if(tx_index<reply_length+7'd3) tx_byte<=reply_payload[tx_index-7'd3];
                    else if(tx_index==reply_length+7'd3) tx_byte<=reply_crc[7:0];else tx_byte<=reply_crc[15:8];
                end
                default: begin state<=S_SYNC;tx_pending<=0;end
                endcase
            end
        end
    end
endmodule
