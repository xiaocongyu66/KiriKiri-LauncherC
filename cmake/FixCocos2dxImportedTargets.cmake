include_guard(GLOBAL)

function(fix_cocos2dx_android_cpufeatures_include)
    if(NOT ANDROID)
        return()
    endif()

    set(_cocos2dx_cpufeatures_dir "")
    foreach(_ndk_root IN ITEMS
        "${ANDROID_NDK}"
        "${CMAKE_ANDROID_NDK}"
        "$ENV{ANDROID_NDK}"
        "$ENV{ANDROID_NDK_HOME}"
        "$ENV{ANDROID_NDK_ROOT}"
    )
        if(_ndk_root AND EXISTS "${_ndk_root}/sources/android/cpufeatures")
            set(_cocos2dx_cpufeatures_dir "${_ndk_root}/sources/android/cpufeatures")
            break()
        endif()
    endforeach()

    foreach(_target_name IN ITEMS
        cocos2dx::cocos2d
        cocos2dx::external
        cocos2dx::ext_cpufeatures
        cocos2dx::cpp_android_spec
    )
        if(NOT TARGET "${_target_name}")
            continue()
        endif()

        get_target_property(_include_dirs "${_target_name}" INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT _include_dirs OR _include_dirs STREQUAL "_include_dirs-NOTFOUND")
            continue()
        endif()

        set(_fixed_include_dirs)
        foreach(_include_dir IN LISTS _include_dirs)
            if(_include_dir MATCHES "/sources/android/cpufeatures/?$")
                if(_cocos2dx_cpufeatures_dir)
                    list(APPEND _fixed_include_dirs "${_cocos2dx_cpufeatures_dir}")
                endif()
            else()
                list(APPEND _fixed_include_dirs "${_include_dir}")
            endif()
        endforeach()

        set_target_properties("${_target_name}" PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_fixed_include_dirs}"
        )
    endforeach()
endfunction()
