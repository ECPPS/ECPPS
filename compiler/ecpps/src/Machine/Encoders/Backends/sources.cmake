list(APPEND BACKEND_SOURCES
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/encoder.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Common/CommonOperations.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Fundamental/Copy.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Fundamental/CopyInteger.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Fundamental/SignExtend.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Fundamental/ZeroExtend.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/Add.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/Sub.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/Shifts.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/BinaryOr.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/BinaryAnd.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/BinaryXor.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/BinaryComplement.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/Arithmetic/ArithmeticNegation.cpp
	${CMAKE_CURRENT_LIST_DIR}/x86_64/Core/Instructions/ControlFlow/Return.cpp
)
