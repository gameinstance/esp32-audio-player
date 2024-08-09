idf_component_register(SRCS "main.cc"
					INCLUDE_DIRS "include"
					PRIV_REQUIRES esp_http_server sdmmc soc driver
					REQUIRES basics audio player spi_bus spi_sd stream_buffer nvs_partition wifi)

if(${ESP_PLATFORM})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mlongcalls -mtext-section-literals")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlongcalls -mtext-section-literals")
endif()
