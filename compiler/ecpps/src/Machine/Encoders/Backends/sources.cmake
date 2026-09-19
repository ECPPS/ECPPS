list(APPEND BACKEND_SOURCES
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/encoder.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Common/CommonOperations.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Fundamental/Copy.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Fundamental/CopyInteger.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/Add.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/Sub.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/ControlFlow/Return.cpp
)
