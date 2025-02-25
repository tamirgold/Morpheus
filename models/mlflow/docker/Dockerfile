# SPDX-FileCopyrightText: Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ARG FROM_IMAGE=gpuci/miniforge-cuda
ARG CUDA_VER=11.5
ARG LINUX_VER=ubuntu20.04
ARG PYTHON_VER=3.8
FROM ${FROM_IMAGE}:${CUDA_VER}-runtime-${LINUX_VER} AS base

WORKDIR /mlflow

COPY . ./

# need to do an upgrade in case gpuci is stale for whatever reason
RUN apt-get update && apt upgrade -y && apt-get install -y procps vim
# but pin python version to avoid some newer CVE
RUN source /etc/profile.d/conda.sh && conda deactivate && conda install python=${PYTHON_VER}
RUN /opt/conda/bin/conda env create -f docker/conda/mlflow-env.yml
RUN sed -i 's/conda activate base/conda activate mlflow/g' ~/.bashrc

SHELL ["/opt/conda/bin/conda", "run", "-n", "mlflow", "/bin/bash", "-c"]

ARG TRITON_DIR=/mlflow/triton-inference-server
ARG TRITON_VER=r23.01

RUN mkdir ${TRITON_DIR} && \
    cd ${TRITON_DIR} && \
    git clone -b ${TRITON_VER} --depth 1 https://github.com/triton-inference-server/server && \
    source activate mlflow && \
    cd ${TRITON_DIR}/server/deploy/mlflow-triton-plugin && \
    pip install .

RUN ln -sf ${TRITON_DIR}/server/deploy/mlflow-triton-plugin/scripts/publish_model_to_mlflow.py /mlflow && \
    mkdir /mlflow/artifacts

# remedy for CVE-2015-20107
RUN find / -name '*mailcap*.*py*' -delete

# remedy for CVE-2022-42919
RUN find / -path '*multiprocessing/util.py' -exec sed -i 's/=\s*_platform_supports_abstract_sockets()/= False/g' {} +

EXPOSE 5000

# Set the entrypoint to use the entrypoint.sh script which sets the conda env
ENTRYPOINT [ "/opt/conda/bin/tini", "--", "docker/entrypoint.sh" ]

CMD [ "/bin/bash" ]
