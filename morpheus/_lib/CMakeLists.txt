# =============================================================================
# Copyright (c) 2020-2023, NVIDIA CORPORATION.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations under
# the License.
# =============================================================================
list(APPEND CMAKE_MESSAGE_CONTEXT "_lib")

find_package(pybind11 REQUIRED)

if(MORPHEUS_PYTHON_INPLACE_BUILD)
  message(STATUS "Performing inplace build for Python modules and library dependencies. Use `pip install -e .`\
   from the root folder to install Morpheus.")
endif()

########### morpheus ###########
set(MORPHEUS_LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

#----------cuda_utils---------
include(cmake/libraries/cuda_utils.cmake)

#----------cudf_helpers---------
include(cmake/libraries/cudf_helpers.cmake)

#----------lib_morpheus---------
include(cmake/libraries/morpheus.cmake)

########### py_morpheus ########
#----------morpheus._lib.stages---------
include(cmake/python_modules/stages.cmake)

#----------morpheus._lib.messages---------
include(cmake/python_modules/messages.cmake)

#----------morpheus._lib.common---------
include(cmake/python_modules/common.cmake)

if (MORPHEUS_BUILD_TESTS)
  add_subdirectory(tests)
endif()

list(POP_BACK CMAKE_MESSAGE_CONTEXT)
