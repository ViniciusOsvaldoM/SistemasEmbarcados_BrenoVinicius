idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES 
                            spi_flash
                            esp_adc
                            nvs_flash
                            esp_event
                            esp_netif
                            mqtt)

