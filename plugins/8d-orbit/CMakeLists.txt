# La sala reutiliza el nucleo DSP de la Reverb de la suite
sd_add_obs_plugin(streamerdesk-asmr-8d-orbit 1.0.0 src/plugin-main.c src/orbit_dsp.c ../reverb/src/reverb_dsp.c)
target_include_directories(streamerdesk-asmr-8d-orbit PRIVATE src)

if(SD_BUILD_TESTS)
  add_executable(test_8d_orbit test/test_orbit.c src/orbit_dsp.c ../reverb/src/reverb_dsp.c)
  target_link_libraries(test_8d_orbit PRIVATE sd_common)
  if(NOT MSVC)
    target_link_libraries(test_8d_orbit PRIVATE m)
  endif()
  add_test(NAME orbit_8d_dsp COMMAND test_8d_orbit)
endif()
