solution "DXR"
    configurations { "Debug", "Release" }
        flags{ "NoPCH" }
        libdirs { "lib" }
        includedirs { "include"}
        platforms{"x64" }

        local location_path = "solution"
        if _ACTION then
	        defines { "_CRT_SECURE_NO_WARNINGS", "NOMINMAX"  }
            location_path = location_path .. "/" .. _ACTION
            location ( location_path )
            location_path = location_path .. "/projects"
        end
    
    disablewarnings { "4251" }

    configuration { "Debug" }
        defines { "DEBUG" }
        symbols "On"
        targetdir ( "bin/" .. "/debug" )

    configuration { "Release" }
        defines { "NDEBUG", "RELEASE" }
        optimize "on"
        floatingpoint "fast"
        targetdir ( "bin/" .. "/release" )

    project "DXR"
        targetname "DXR"
		debugdir ""
        defines {}
		location ( location_path )
		language "C++"
		kind "ConsoleApp"
		files { "src/**"}
		systemversion "10.0.16299.0"
		includedirs { "include", "src" }
		links {"glfw3", "d3d12", "dxgi"}
        configuration{"Release"}
            links {"FallbackLayer"}
        configuration{"Debug"}
            links {"FallbackLayerD"}
