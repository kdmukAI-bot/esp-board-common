# Register the OV02C10 IPA (Image Process Algorithm) tuning file with esp_ipa.
#
# esp_ipa compiles every JSON listed in the ESP_IPA_JSON_CONFIG_FILE_PATH build
# property into one generated lookup table (esp_video_ipa_config.c). Without an
# entry here esp_ipa generates a stub whose esp_ipa_pipeline_get_config() always
# returns NULL, no IPA pipeline is created, and the sensor runs with whatever
# fixed exposure/gain/white-balance it powered up with. This mirrors what
# esp_cam_sensor's own project_include.cmake does for its built-in sensors.
#
# The property is consumed during esp_ipa's component build, so it MUST be set
# from a project_include.cmake (evaluated for every component before any of them
# are configured) rather than from this component's CMakeLists.txt.
#
# Tuning file choice: the vendor ships per-silicon-revision tuning. Revisions
# below v3 use the "eco4" file (no lens-shading/black-level correction blocks and
# a fixed CCM); v3 and later use "eco5". Only the eco4 lineage is carried here —
# add an eco5 file and a revision test if an eco5-class part is ever targeted.

if(CONFIG_CAMERA_OV02C10)
    if(CONFIG_CAMERA_OV02C10_DEFAULT_IPA_JSON_CONFIGURATION_FILE)
        idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH
            "${COMPONENT_PATH}/cfg/ov02c10_seedsigner_p4_eco4.json" APPEND)
    elseif(CONFIG_CAMERA_OV02C10_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE)
        idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH
            ${CONFIG_CAMERA_OV02C10_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH} APPEND)
    endif()
endif()
