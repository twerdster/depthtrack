// Mojo V3 top; UART/pins from the MIT-licensed Embedded Micro base project.
// Unused expansion GPIOs are not instantiated. ADC channel15 disables ADC.
module mojo_top #(parameter MAX_LEVELS=10) (
    input clk, input rst_n, input cclk, output [7:0] led,
    output spi_miso, input spi_ss, input spi_mosi, input spi_sck,
    output [3:0] spi_channel, input avr_tx, output avr_rx, input avr_rx_busy
);
    reg [2:0] reset_sync = 3'b111;
    always @(posedge clk) reset_sync <= {reset_sync[1:0], !rst_n};
    wire rst = |reset_sync;
    wire avr_ready;
    cclk_detector #(.CLK_RATE(50000000)) startup(clk, rst, cclk, avr_ready);
    wire [7:0] rx_data, tx_data;
    wire rx_valid, rx_ready, tx_valid, tx_busy;
    wire tx_ready = avr_ready && !tx_busy;
    wire [15:0] loaded_count;
    // rx_ready is protocol flow control for simulation/stream adapters.
    // The physical UART cannot stall received bytes: the host must obey the
    // stop-and-wait protocol, sending no new packet until its response.
    tree_protocol #(.MAX_LEVELS(MAX_LEVELS),.CONTEXTS(4)) protocol (
        .clk(clk), .reset(rst || !avr_ready),
        .rx_data(rx_data), .rx_valid(rx_valid), .rx_ready(rx_ready),
        .tx_data(tx_data), .tx_valid(tx_valid), .tx_ready(tx_ready),
        .loaded_count(loaded_count)
    );
    depth_tree_avr_bridge #(.CLK_RATE(50000000), .SERIAL_BAUD_RATE(500000)) bridge (
        .clk(clk), .rst(rst), .cclk(cclk),
        .spi_miso(spi_miso), .spi_mosi(spi_mosi), .spi_sck(spi_sck), .spi_ss(spi_ss),
        .spi_channel(spi_channel), .tx(avr_rx), .rx(avr_tx), .channel(4'hf),
        .new_sample(), .sample(), .sample_channel(),
        .tx_data(tx_data), .new_tx_data(tx_valid && tx_ready),
        .tx_busy(tx_busy), .tx_block(avr_rx_busy),
        .rx_data(rx_data), .new_rx_data(rx_valid)
    );
    assign led = {avr_ready, loaded_count == 16'd16384, 2'b0, loaded_count[11:8]};
endmodule
