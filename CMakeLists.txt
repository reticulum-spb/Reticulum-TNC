cmake_minimum_required(VERSION 3.20)

project(reticulum_tnc VERSION 0.1.0 LANGUAGES C)

include(CTest)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_package(Threads QUIET)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(RTNC_REQUIRE_LIQUIDDSP "Fail configuration when LiquidDSP is unavailable" ON)

find_package(LiquidDSP QUIET)
if(NOT LiquidDSP_FOUND)
    if(RTNC_REQUIRE_LIQUIDDSP)
        message(FATAL_ERROR
            "LiquidDSP was not found. Install its development package, or use "
            "-DRTNC_REQUIRE_LIQUIDDSP=OFF for profile-only skeleton tests.")
    endif()
    message(WARNING "LiquidDSP unavailable: only DSP-independent skeleton code is built")
endif()

add_library(rtnc_core STATIC
    src/fec/fec.c
    src/fec/ldpc.c
    src/framing/fragmentation.c
    src/framing/frame.c
    src/modem/rate.c
    src/platform/audio_ring.c
    src/platform/burst_detector.c
    src/platform/completion.c
    src/platform/decode_queue.c
    src/platform/kiss.c
    src/platform/ota_benchmark.c
    src/platform/packet_queue.c
    src/platform/runtime.c
    src/platform/tx_eq.c
    src/platform/wav.c
    src/phy/profile.c
)
add_library(rtnc::core ALIAS rtnc_core)

target_compile_features(rtnc_core PUBLIC c_std_11)
target_include_directories(rtnc_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
if(LiquidDSP_FOUND)
    target_sources(rtnc_core PRIVATE
        src/phy/acquisition.c
        src/phy/carrier.c
        src/phy/equalizer.c
        src/modem/offline.c
        src/phy/psk.c
        src/phy/radio_response.c
        src/phy/rrc.c
        src/phy/timing.c
    )
    target_link_libraries(rtnc_core PUBLIC LiquidDSP::LiquidDSP)
    target_compile_definitions(rtnc_core PUBLIC RTNC_HAVE_LIQUIDDSP=1)

    add_executable(rtnc_generate_hackrf_nfm tools/generate_hackrf_nfm.c)
    target_link_libraries(rtnc_generate_hackrf_nfm PRIVATE rtnc::core m)
    add_executable(rtnc_decode_wav tools/decode_wav.c)
    target_link_libraries(rtnc_decode_wav PRIVATE rtnc::core m)
    add_executable(rtnc_hackrf_nfm_capture tools/hackrf_nfm_capture.c)
    target_link_libraries(rtnc_hackrf_nfm_capture PRIVATE
        LiquidDSP::LiquidDSP m)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_generate_hackrf_nfm PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_decode_wav PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_hackrf_nfm_capture PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
endif()

if(BUILD_TESTING)
    find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
    find_library(HACKRF_LIBRARY NAMES hackrf)
    find_path(CODEC2_INCLUDE_DIR NAMES codec2/freedv_api.h)
    find_library(CODEC2_LIBRARY NAMES codec2)
endif()

if(BUILD_TESTING AND LiquidDSP_FOUND AND CODEC2_INCLUDE_DIR AND CODEC2_LIBRARY)
    add_executable(rtnc_freedv_iq_decode tools/freedv_iq_decode.c)
    target_include_directories(rtnc_freedv_iq_decode PRIVATE
        ${CODEC2_INCLUDE_DIR})
    target_link_libraries(rtnc_freedv_iq_decode PRIVATE
        LiquidDSP::LiquidDSP ${CODEC2_LIBRARY} m)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_freedv_iq_decode PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
endif()

add_executable(rtnc_analyze_iq tools/analyze_iq.c)
target_link_libraries(rtnc_analyze_iq PRIVATE m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rtnc_analyze_iq PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

add_executable(rtnc_kiss_benchmark tools/kiss_benchmark.c)
target_compile_features(rtnc_kiss_benchmark PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rtnc_kiss_benchmark PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()


if(BUILD_TESTING AND LiquidDSP_FOUND AND HACKRF_INCLUDE_DIR AND HACKRF_LIBRARY AND Threads_FOUND)
    add_library(rtnc_hackrf STATIC src/platform/hackrf_tx.c)
    target_compile_features(rtnc_hackrf PUBLIC c_std_11)
    target_include_directories(rtnc_hackrf PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${HACKRF_INCLUDE_DIR})
    target_link_libraries(rtnc_hackrf PUBLIC
        ${HACKRF_LIBRARY} Threads::Threads m)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_hackrf PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()

    add_executable(rtnc_hackrf_rx tools/hackrf_rx.c)
    target_include_directories(rtnc_hackrf_rx PRIVATE ${HACKRF_INCLUDE_DIR})
    target_link_libraries(rtnc_hackrf_rx PRIVATE
        rtnc::core ${HACKRF_LIBRARY} Threads::Threads m)
    add_executable(rtnc_hackrf_tx tools/hackrf_tx.c)
    target_link_libraries(rtnc_hackrf_tx PRIVATE rtnc::core rtnc_hackrf m)
    add_executable(rtnc_hackrf_sounder tools/hackrf_sounder.c)
    target_link_libraries(rtnc_hackrf_sounder PRIVATE rtnc_hackrf m)
    add_executable(rtnc_hackrf_nfm_meter tools/hackrf_nfm_meter.c)
    target_include_directories(rtnc_hackrf_nfm_meter PRIVATE
        ${HACKRF_INCLUDE_DIR})
    target_link_libraries(rtnc_hackrf_nfm_meter PRIVATE
        LiquidDSP::LiquidDSP ${HACKRF_LIBRARY} Threads::Threads m)
    if(BUILD_TESTING)
        add_executable(test_hackrf_tx_config
            tests/unit/test_hackrf_tx_config.c)
        target_link_libraries(test_hackrf_tx_config PRIVATE rtnc_hackrf)
        add_test(NAME hackrf_tx_configuration COMMAND test_hackrf_tx_config)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_hackrf_rx PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_hackrf_tx PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_hackrf_sounder PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_hackrf_nfm_meter PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        if(TARGET test_hackrf_tx_config)
            target_compile_options(test_hackrf_tx_config PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rtnc_core PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    )
endif()

find_path(CYAML_INCLUDE_DIR NAMES cyaml/cyaml.h)
find_library(CYAML_LIBRARY NAMES cyaml)
find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h)
find_library(GPIOD_LIBRARY NAMES gpiod)
find_path(ALSA_INCLUDE_DIR NAMES alsa/asoundlib.h)
find_library(ALSA_LIBRARY NAMES asound)
if(CYAML_INCLUDE_DIR AND CYAML_LIBRARY AND GPIOD_INCLUDE_DIR AND GPIOD_LIBRARY
        AND ALSA_INCLUDE_DIR AND ALSA_LIBRARY AND Threads_FOUND)
    add_library(rtnc_platform STATIC
        src/platform/alsa_runtime_backend.c
        src/platform/audio.c
        src/platform/config.c
        src/platform/ptt.c
        src/platform/transmitter.c)
    target_compile_features(rtnc_platform PUBLIC c_std_11)
    target_include_directories(rtnc_platform PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CYAML_INCLUDE_DIR} ${GPIOD_INCLUDE_DIR} ${ALSA_INCLUDE_DIR})
    target_link_libraries(rtnc_platform PUBLIC
        rtnc::core ${CYAML_LIBRARY} ${GPIOD_LIBRARY} ${ALSA_LIBRARY}
        Threads::Threads m)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_platform PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()

    add_executable(rtnc_check_config tools/check_config.c)
    target_link_libraries(rtnc_check_config PRIVATE rtnc_platform)
    add_executable(rtnc_audio_smoke tools/audio_smoke.c)
    target_link_libraries(rtnc_audio_smoke PRIVATE rtnc_platform)
    add_executable(rtnc_alsa_sounder tools/alsa_sounder.c)
    target_link_libraries(rtnc_alsa_sounder PRIVATE rtnc_platform m)
    add_executable(rtnc_radio_calibrate tools/radio_calibrate.c)
    target_link_libraries(rtnc_radio_calibrate PRIVATE rtnc_platform m)
    if(LiquidDSP_FOUND)
        add_executable(rtnc_tx_smoke tools/tx_smoke.c)
        target_link_libraries(rtnc_tx_smoke PRIVATE rtnc_platform m)
        add_executable(rtnc_packet_tx tools/packet_tx.c)
        target_link_libraries(rtnc_packet_tx PRIVATE rtnc_platform m)
        add_executable(rtnc_alsa_rx tools/alsa_rx.c)
        target_link_libraries(rtnc_alsa_rx PRIVATE rtnc_platform m)
        add_executable(rtnc_modem tools/alsa_rx.c)
        target_link_libraries(rtnc_modem PRIVATE rtnc_platform m)
        add_executable(rtnc_ota_benchmark tools/ota_benchmark.c)
        target_link_libraries(rtnc_ota_benchmark PRIVATE rtnc_platform m)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_check_config PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_audio_smoke PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_alsa_sounder PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        target_compile_options(rtnc_radio_calibrate PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        if(TARGET rtnc_tx_smoke)
            target_compile_options(rtnc_tx_smoke PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
        if(TARGET rtnc_packet_tx)
            target_compile_options(rtnc_packet_tx PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
        if(TARGET rtnc_alsa_rx)
            target_compile_options(rtnc_alsa_rx PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
        if(TARGET rtnc_modem)
            target_compile_options(rtnc_modem PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
        if(TARGET rtnc_ota_benchmark)
            target_compile_options(rtnc_ota_benchmark PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endif()
endif()

if(BUILD_TESTING AND NOT TARGET rtnc_radio_calibrate AND LiquidDSP_FOUND AND
        HACKRF_INCLUDE_DIR AND HACKRF_LIBRARY)
    add_executable(rtnc_radio_calibrate tools/radio_calibrate_hackrf.c)
    target_include_directories(rtnc_radio_calibrate PRIVATE
        ${HACKRF_INCLUDE_DIR})
    target_link_libraries(rtnc_radio_calibrate PRIVATE
        rtnc::core LiquidDSP::LiquidDSP ${HACKRF_LIBRARY} m)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rtnc_radio_calibrate PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(test_phy_profile tests/unit/test_phy_profile.c)
    target_link_libraries(test_phy_profile PRIVATE rtnc::core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_phy_profile PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        )
    endif()
    add_test(NAME phy_profile COMMAND test_phy_profile)

    add_executable(test_tx_eq tests/unit/test_tx_eq.c)
    target_link_libraries(test_tx_eq PRIVATE rtnc::core m)
    add_test(NAME tx_inverse_response_fir COMMAND test_tx_eq)

    add_executable(test_radio_response tests/unit/test_radio_response.c)
    target_link_libraries(test_radio_response PRIVATE rtnc::core m)
    add_test(NAME radio_complex_response COMMAND test_radio_response)

    add_executable(test_psk_soft tests/unit/test_psk_soft.c)
    target_link_libraries(test_psk_soft PRIVATE rtnc::core m)
    add_test(NAME psk_soft_mapping COMMAND test_psk_soft)

    add_executable(test_wav tests/unit/test_wav.c)
    target_link_libraries(test_wav PRIVATE rtnc::core)
    add_test(NAME wav_round_trip COMMAND test_wav)

    add_executable(test_audio_ring tests/unit/test_audio_ring.c)
    target_link_libraries(test_audio_ring PRIVATE rtnc::core)
    add_test(NAME audio_spsc_ring COMMAND test_audio_ring)

    add_executable(test_packet_queue tests/unit/test_packet_queue.c)
    target_link_libraries(test_packet_queue PRIVATE rtnc::core)
    add_test(NAME packet_spsc_queue COMMAND test_packet_queue)

    add_executable(test_kiss tests/unit/test_kiss.c)
    target_link_libraries(test_kiss PRIVATE rtnc::core)
    add_test(NAME kiss_stream_framing COMMAND test_kiss)

    add_executable(test_ota_benchmark tests/unit/test_ota_benchmark.c)
    target_link_libraries(test_ota_benchmark PRIVATE rtnc::core)
    add_test(NAME ota_benchmark_protocol COMMAND test_ota_benchmark)

    add_executable(test_decode_queue tests/unit/test_decode_queue.c)
    target_link_libraries(test_decode_queue PRIVATE rtnc::core)
    add_test(NAME decode_spsc_queue COMMAND test_decode_queue)

    add_executable(test_completion tests/unit/test_completion.c)
    target_link_libraries(test_completion PRIVATE rtnc::core)
    add_test(NAME decoder_completion_ordering COMMAND test_completion)

    add_executable(test_fragmentation tests/unit/test_fragmentation.c)
    target_link_libraries(test_fragmentation PRIVATE rtnc::core)
    add_test(NAME packet_fragmentation_reassembly COMMAND test_fragmentation)

    add_executable(test_burst_detector tests/unit/test_burst_detector.c)
    target_link_libraries(test_burst_detector PRIVATE rtnc::core)
    add_test(NAME streaming_burst_detector COMMAND test_burst_detector)

    if(LiquidDSP_FOUND)
        add_executable(test_runtime tests/unit/test_runtime.c)
        target_link_libraries(test_runtime PRIVATE rtnc::core m)
        add_test(NAME packet_runtime_state_machine COMMAND test_runtime)

        add_executable(test_acquisition_detector
            tests/unit/test_acquisition_detector.c)
        target_link_libraries(test_acquisition_detector PRIVATE rtnc::core m)
        add_test(NAME streaming_acquisition_detector
            COMMAND test_acquisition_detector)
    endif()

    add_executable(test_frame tests/unit/test_frame.c)
    target_link_libraries(test_frame PRIVATE rtnc::core)
    add_test(NAME frame_crc_and_validation COMMAND test_frame)

    add_executable(test_fec tests/unit/test_fec.c)
    target_link_libraries(test_fec PRIVATE rtnc::core)
    add_test(NAME fec_none_soft_decode COMMAND test_fec)

    add_executable(test_ldpc_decoder tests/unit/test_ldpc_decoder.c)
    target_link_libraries(test_ldpc_decoder PRIVATE rtnc::core)
    add_test(NAME ldpc_normalized_min_sum COMMAND test_ldpc_decoder)

    add_executable(test_ccsds_tc128 tests/unit/test_ccsds_tc128.c)
    target_link_libraries(test_ccsds_tc128 PRIVATE rtnc::core)
    add_test(NAME ccsds_tc128_codec COMMAND test_ccsds_tc128)

    add_executable(test_ccsds_tc512 tests/unit/test_ccsds_tc512.c)
    target_link_libraries(test_ccsds_tc512 PRIVATE rtnc::core)
    add_test(NAME ccsds_tc512_codec COMMAND test_ccsds_tc512)

    add_executable(test_fec_ebn0 tests/channel/test_fec_ebn0.c)
    target_link_libraries(test_fec_ebn0 PRIVATE rtnc::core m)
    add_test(NAME fec_calibrated_ebn0 COMMAND test_fec_ebn0)

    add_executable(test_fec_early_reject tests/unit/test_fec_early_reject.c)
    target_link_libraries(test_fec_early_reject PRIVATE rtnc::core)
    add_test(NAME fec_multiblock_early_reject COMMAND test_fec_early_reject)

    if(Python3_Interpreter_FOUND)
        add_test(NAME ldpc_generated_tables
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ldpc_table.py
                --input ${CMAKE_CURRENT_SOURCE_DIR}/tools/codes/ldpc_test_7_4.json
                --output ${CMAKE_CURRENT_SOURCE_DIR}/src/fec/codes/ldpc_test_7_4.h
                --check)
        add_test(NAME ccsds_tc128_generated_table
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ldpc_table.py
                --input ${CMAKE_CURRENT_SOURCE_DIR}/tools/codes/ccsds_tc128.json
                --output ${CMAKE_CURRENT_SOURCE_DIR}/src/fec/codes/ccsds_tc128.h
                --check)
        add_test(NAME ccsds_tc512_generated_table
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_ldpc_table.py
                --input ${CMAKE_CURRENT_SOURCE_DIR}/tools/codes/ccsds_tc512.json
                --output ${CMAKE_CURRENT_SOURCE_DIR}/src/fec/codes/ccsds_tc512.h
                --check)
    endif()

    if(LiquidDSP_FOUND)
        add_executable(test_equalizer tests/unit/test_equalizer.c)
        target_link_libraries(test_equalizer PRIVATE rtnc::core m)
        add_test(NAME equalizer_fractional_rls COMMAND test_equalizer)

        add_executable(test_baseband_loopback tests/loopback/test_baseband_loopback.c)
        target_link_libraries(test_baseband_loopback PRIVATE rtnc::core m)
        add_test(NAME baseband_rrc_loopback COMMAND test_baseband_loopback)

        add_executable(test_timing_recovery tests/channel/test_timing_recovery.c)
        target_link_libraries(test_timing_recovery PRIVATE rtnc::core m)
        add_test(NAME timing_recovery_ppm COMMAND test_timing_recovery)

        add_executable(test_audio_loopback tests/loopback/test_audio_loopback.c)
        target_link_libraries(test_audio_loopback PRIVATE rtnc::core m)
        add_test(NAME audio_carrier_loopback COMMAND test_audio_loopback)

        add_executable(test_phy_rates tests/loopback/test_phy_rates.c)
        target_link_libraries(test_phy_rates PRIVATE rtnc::core m)
        add_test(NAME modem_fixed_phy_rates COMMAND test_phy_rates)

        add_executable(test_psk_profiles tests/loopback/test_psk_profiles.c)
        target_link_libraries(test_psk_profiles PRIVATE rtnc::core m)
        add_test(NAME modem_fixed_psk_profiles COMMAND test_psk_profiles)

        add_executable(test_modem_loopback tests/loopback/test_modem_loopback.c)
        target_link_libraries(test_modem_loopback PRIVATE rtnc::core m)
        add_test(NAME modem_payload_audio_loopback COMMAND test_modem_loopback)

        add_executable(test_channel_impairments tests/channel/test_impairments.c)
        target_link_libraries(test_channel_impairments PRIVATE rtnc::core m)
        add_test(NAME channel_awgn_phase_cfo COMMAND test_channel_impairments)

        add_executable(test_bpsk_channel tests/channel/test_bpsk_channel.c)
        target_link_libraries(test_bpsk_channel PRIVATE rtnc::core m)
        add_test(NAME bpsk_phase_cfo_timing COMMAND test_bpsk_channel)

        add_executable(test_modem_ebn0 tests/channel/test_modem_ebn0.c)
        target_link_libraries(test_modem_ebn0 PRIVATE rtnc::core m)
        add_test(NAME modem_calibrated_ebn0 COMMAND test_modem_ebn0)

        add_executable(test_phy_rate_ebn0
            tests/channel/test_phy_rate_ebn0.c)
        target_link_libraries(test_phy_rate_ebn0 PRIVATE rtnc::core m)
        add_test(NAME phy_rate_calibrated_ebn0 COMMAND test_phy_rate_ebn0)

        add_executable(test_payload_classes tests/loopback/test_payload_classes.c)
        target_link_libraries(test_payload_classes PRIVATE rtnc::core m)
        add_test(NAME modem_payload_classes COMMAND test_payload_classes)

        add_executable(test_payload_class_ebn0
            tests/channel/test_payload_class_ebn0.c)
        target_link_libraries(test_payload_class_ebn0 PRIVATE rtnc::core m)
        add_test(NAME payload_class_calibrated_ebn0
            COMMAND test_payload_class_ebn0)

        add_executable(test_sample_rate_mismatch
            tests/channel/test_sample_rate_mismatch.c)
        target_link_libraries(test_sample_rate_mismatch PRIVATE rtnc::core m)
        add_test(NAME channel_sample_rate_mismatch
            COMMAND test_sample_rate_mismatch)

        add_executable(test_timing_noise tests/channel/test_timing_noise.c)
        target_link_libraries(test_timing_noise PRIVATE rtnc::core m)
        add_test(NAME channel_timing_noise_matrix COMMAND test_timing_noise)

        add_executable(test_audio_distortion
            tests/channel/test_audio_distortion.c)
        target_link_libraries(test_audio_distortion PRIVATE rtnc::core m)
        add_test(NAME channel_audio_distortion COMMAND test_audio_distortion)

        add_executable(test_phy_rate_voice_band
            tests/channel/test_phy_rate_voice_band.c)
        target_link_libraries(test_phy_rate_voice_band PRIVATE rtnc::core m)
        add_test(NAME phy_rate_voice_band COMMAND test_phy_rate_voice_band)

        add_executable(test_equalizer_audio_echo
            tests/channel/test_equalizer_audio_echo.c)
        target_link_libraries(test_equalizer_audio_echo PRIVATE rtnc::core m)
        add_test(NAME equalizer_audio_echo COMMAND test_equalizer_audio_echo)

        add_executable(test_equalizer_state
            tests/channel/test_equalizer_state.c)
        target_link_libraries(test_equalizer_state PRIVATE rtnc::core m)
        add_test(NAME equalizer_state_restoration COMMAND test_equalizer_state)

        add_executable(test_measured_radio_response
            tests/channel/test_measured_radio_response.c)
        target_link_libraries(test_measured_radio_response PRIVATE rtnc::core m)
        add_test(NAME measured_radio_complex_replay
            COMMAND test_measured_radio_response)

        add_test(NAME recorded_hackrf_handheld_replay
            COMMAND rtnc_decode_wav
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/recordings/hackrf_radio_dev2100_frame.wav)
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        foreach(test_target IN ITEMS
                test_wav test_tx_eq test_audio_ring test_decode_queue test_burst_detector
                test_acquisition_detector
                test_frame test_fec test_ldpc_decoder test_ccsds_tc128
                test_ccsds_tc512
                test_fec_ebn0
                test_modem_ebn0
                test_baseband_loopback test_timing_recovery
                test_audio_loopback test_modem_loopback test_psk_profiles
                test_payload_classes
                test_payload_class_ebn0 test_timing_noise test_equalizer
                test_audio_distortion test_equalizer_audio_echo test_bpsk_channel
                test_equalizer_state test_measured_radio_response)
        if(TARGET test_sample_rate_mismatch)
            target_compile_options(test_sample_rate_mismatch PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
            if(TARGET ${test_target})
                target_compile_options(${test_target} PRIVATE
                    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                )
            endif()
        endforeach()
        if(TARGET test_channel_impairments)
            target_compile_options(test_channel_impairments PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        endif()
    endif()
endif()
