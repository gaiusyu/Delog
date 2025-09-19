# =========================================================================
# Copyright (C) 2016-2023 LOGPAI (https://github.com/logpai).
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# =========================================================================

import sys
sys.path.append("../../")
from Log_Parsers.LogCluster import LogParser
from Log_Parsers.utils import evaluator
import os
import pandas as pd


input_dir = "../../loghub_data/"  # The input directory of log file
output_dir = "Drain_result/"  # The output directory of parsing results

benchmark_settings = {

    "HealthApp": {
        "log_file": "HealthApp/HealthApp_full.log",
        "log_format": "<Time>\|<Component>\|<Pid>\|<Content>",
        "regex": [],
        "rsupport": 7,
    },

}

bechmark_result = []
for dataset, setting in benchmark_settings.items():
    print("\n=== Evaluation on %s ===" % dataset)
    indir = os.path.join(input_dir, os.path.dirname(setting["log_file"]))
    log_file = os.path.basename(setting["log_file"])

    parser = LogParser(
        indir,
        setting["log_format"],
        output_dir,
        rex=setting["regex"],
        rsupport=setting["rsupport"],
    )
    parser.parse(log_file)

    F1_measure, accuracy = evaluator.evaluate(
        groundtruth=os.path.join(indir, log_file + "_structured.csv"),
        parsedresult=os.path.join(output_dir, log_file + "_structured.csv"),
    )
    bechmark_result.append([dataset, F1_measure, accuracy])

print("\n=== Overall evaluation results ===")
df_result = pd.DataFrame(bechmark_result, columns=["Dataset", "F1_measure", "Accuracy"])
df_result.set_index("Dataset", inplace=True)
print(df_result)
df_result.to_csv("LogCluster_bechmark_result.csv", float_format="%.6f")
