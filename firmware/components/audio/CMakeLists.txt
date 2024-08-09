idf_component_register(SRCS "src/wave.cc" "src/flac.cc"
					INCLUDE_DIRS "include"
					REQUIRES stream basics
					LDFRAGMENTS "libaudio.lf"
)
