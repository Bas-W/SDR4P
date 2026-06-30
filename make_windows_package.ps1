$build_dir=$args[0]
$root_dir=$args[1]

mkdir sdr4p_windows_x64

# Copy root
cp -Recurse $root_dir/* sdr4p_windows_x64/

# Copy core
cp $build_dir/Release/* sdr4p_windows_x64/
cp 'C:/Program Files/PothosSDR/bin/volk.dll' sdr4p_windows_x64/

# Copy source modules
cp $build_dir/source_modules/airspy_source/Release/airspy_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/airspy.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/airspyhf_source/Release/airspyhf_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/airspyhf.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/audio_source/Release/audio_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/bladerf_source/Release/bladerf_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/bladeRF.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/file_source/Release/file_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/fobossdr_source/Release/fobossdr_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/RigExpert/Fobos/bin/fobos.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/hackrf_source/Release/hackrf_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/hackrf.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/hermes_source/Release/hermes_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/hydrasdr_source/Release/hydrasdr_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/hydrasdr-host/bin/hydrasdr.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/limesdr_source/Release/limesdr_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/LimeSuite.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/network_source/Release/network_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/perseus_source/Release/perseus_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/perseus-sdr.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/plutosdr_source/Release/plutosdr_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/libiio.dll' sdr4p_windows_x64/
cp 'C:/Program Files/PothosSDR/bin/libad9361.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/rfnm_source/Release/rfnm_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/RFNM/bin/rfnm.dll' sdr4p_windows_x64/
cp 'C:/Program Files/RFNM/bin/spdlog.dll' sdr4p_windows_x64/
cp 'C:/Program Files/RFNM/bin/fmt.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/rfspace_source/Release/rfspace_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/rtl_sdr_source/Release/rtl_sdr_source.dll sdr4p_windows_x64/modules/
cp 'C:/Program Files/PothosSDR/bin/rtlsdr.dll' sdr4p_windows_x64/

cp $build_dir/source_modules/rtl_tcp_source/Release/rtl_tcp_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/sdrplay_source/Release/sdrplay_source.dll sdr4p_windows_x64/modules/ -ErrorAction SilentlyContinue
cp 'C:/Program Files/SDRplay/API/x64/sdrplay_api.dll' sdr4p_windows_x64/ -ErrorAction SilentlyContinue

cp $build_dir/source_modules/sdrpp_server_source/Release/sdrpp_server_source.dll sdr4p_windows_x64/modules/

cp $build_dir/source_modules/spyserver_source/Release/spyserver_source.dll sdr4p_windows_x64/modules/

# cp $build_dir/source_modules/usrp_source/Release/usrp_source.dll sdr4p_windows_x64/modules/


# Copy sink modules
cp $build_dir/sink_modules/audio_sink/Release/audio_sink.dll sdr4p_windows_x64/modules/
cp "C:/Program Files (x86)/RtAudio/bin/rtaudio.dll" sdr4p_windows_x64/

cp $build_dir/sink_modules/network_sink/Release/network_sink.dll sdr4p_windows_x64/modules/


# Copy decoder modules
cp $build_dir/decoder_modules/atv_decoder/Release/atv_decoder.dll sdr4p_windows_x64/modules/

cp $build_dir/decoder_modules/m17_decoder/Release/m17_decoder.dll sdr4p_windows_x64/modules/
cp "C:/Program Files/codec2/lib/libcodec2.dll" sdr4p_windows_x64/

cp $build_dir/decoder_modules/meteor_demodulator/Release/meteor_demodulator.dll sdr4p_windows_x64/modules/

cp $build_dir/decoder_modules/radio/Release/radio.dll sdr4p_windows_x64/modules/


# Copy misc modules
cp $build_dir/misc_modules/discord_integration/Release/discord_integration.dll sdr4p_windows_x64/modules/

cp $build_dir/misc_modules/frequency_manager/Release/frequency_manager.dll sdr4p_windows_x64/modules/

cp $build_dir/misc_modules/iq_exporter/Release/iq_exporter.dll sdr4p_windows_x64/modules/

cp $build_dir/misc_modules/recorder/Release/recorder.dll sdr4p_windows_x64/modules/

cp $build_dir/misc_modules/rigctl_client/Release/rigctl_client.dll sdr4p_windows_x64/modules/

cp $build_dir/misc_modules/rigctl_server/Release/rigctl_server.dll sdr4p_windows_x64/modules/

cp $build_dir/misc_modules/scanner/Release/scanner.dll sdr4p_windows_x64/modules/


# Copy supporting libs
cp 'C:/Program Files/PothosSDR/bin/libusb-1.0.dll' sdr4p_windows_x64/
cp 'C:/Program Files/PothosSDR/bin/pthreadVC2.dll' sdr4p_windows_x64/

Compress-Archive -Path sdr4p_windows_x64/ -DestinationPath sdr4p_windows_x64.zip

rm -Force -Recurse sdr4p_windows_x64
