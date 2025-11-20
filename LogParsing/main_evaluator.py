# main_evaluator.py

import os
import pandas as pd
import re
import time
from collections import defaultdict

from evaluation_logic import get_group_accuracy, evaluate_template_level

class AdvancedSimpleParser:
    def __init__(self, indir, outdir):
        self.indir = indir
        self.outdir = outdir
        self.delimiters_regex = r'(\s+|[,\;<span data-type="block-math" data-value=""></span>\_\+\=\:\@\#\"\{\}<span data-type="inline-math" data-value=""></span>])'

    def _is_variable(self, token):
        has_digit = False
        has_special = False
        for char in token:
            if char.isdigit():
                has_digit = True
            elif not char.isalpha():
                has_special = True
        
        return has_digit or has_special

    def parse(self, log_file_basename):
        log_filepath = os.path.join(self.indir, log_file_basename)
        output_filepath = os.path.join(self.outdir, f"{log_file_basename}_structured.csv")
        
        print(f"Parsing {log_filepath} with AdvancedSimpleParser...")
        parsed_logs = []
        template_map = {}
        
        start_time = time.time()
        
        with open(log_filepath, 'r', encoding='utf-8', errors='ignore') as f:
            for line_id, line in enumerate(f.readlines()):
                content = line.strip()
                if not content:
                    continue

                template_tokens = []
                parameter_list = []
                
                tokens = re.split(self.delimiters_regex, content)
                
                for token in tokens:
                    if not token:
                        continue
                    
                    if re.fullmatch(self.delimiters_regex, token):
                        template_tokens.append(token)
                        continue

                    if self._is_variable(token):
                        template_tokens.append('<*>')
                        parameter_list.append(token)
                    else:
                        template_tokens.append(token)
                
                event_template = ''.join(template_tokens)
                
                if event_template not in template_map:
                    template_map[event_template] = f"E{len(template_map)}"
                event_id = template_map[event_template]
                
                parsed_logs.append({
                    'LineId': line_id + 1,
                    'Content': content,
                    'EventId': event_id,
                    'EventTemplate': event_template,
                    'ParameterList': str(parameter_list)
                })
        
        parse_time = time.time() - start_time
        
        df_parsed = pd.DataFrame(parsed_logs)
        df_parsed.to_csv(output_filepath, index=False)
        
        print(f"Parsing finished in {parse_time:.2f}s. Results saved to {output_filepath}")
        return parse_time


def main():
    datasets = [
        "Android", "Apache", "BGL", "HDFS", "HPC", "Hadoop", "HealthApp", 
        "Linux", "Mac", "OpenSSH", "OpenStack", "Proxifier", "Spark", 
        "Thunderbird", "Windows", "Zookeeper"
    ]
    
    log_base_dir = 'logs'
    results_base_dir = 'results'
    all_results = []

    for dataset in datasets:
        print(f"\n{'='*20} Evaluating on: {dataset} {'='*20}")
        
        indir = os.path.join(log_base_dir, dataset)
        outdir = os.path.join(results_base_dir, dataset)
        os.makedirs(outdir, exist_ok=True)
        
        log_file_basename = f"{dataset}_2k.log"
        groundtruth_file = os.path.join(indir, f"{log_file_basename}_structured.csv")
        
        if not os.path.exists(os.path.join(indir, log_file_basename)) or not os.path.exists(groundtruth_file):
            print(f"Skipping {dataset}: Log file or ground truth file not found.")
            continue

        parser = AdvancedSimpleParser(indir=indir, outdir=outdir)
        parse_time = parser.parse(log_file_basename)
        parsed_result_file = os.path.join(outdir, f"{log_file_basename}_structured.csv")

        try:
            df_groundtruth = pd.read_csv(groundtruth_file, dtype=str).fillna('')
            df_parsedlog = pd.read_csv(parsed_result_file, dtype=str).fillna('')
        except Exception as e:
            print(f"Error loading files for {dataset}: {e}")
            continue

        df_groundtruth['LineId'] = df_groundtruth['LineId'].astype(int)
        df_parsedlog['LineId'] = df_parsedlog['LineId'].astype(int)
        df_groundtruth.set_index('LineId', inplace=True)
        df_parsedlog.set_index('LineId', inplace=True)
        
        common_ids = df_groundtruth.index.intersection(df_parsedlog.index)
        df_groundtruth = df_groundtruth.loc[common_ids]
        df_parsedlog = df_parsedlog.loc[common_ids]

        series_groundtruth = df_groundtruth['EventTemplate']
        series_parsedlog = df_parsedlog['EventTemplate']
        
        GA, FGA = get_group_accuracy(series_groundtruth, series_parsedlog)

        correctly_parsed_messages = df_parsedlog.reset_index()[['EventTemplate']].eq(df_groundtruth.reset_index()[['EventTemplate']]).values.sum()
        total_messages = len(df_parsedlog)
        PA = float(correctly_parsed_messages) / total_messages if total_messages > 0 else 0

        tool_templates, ground_templates, FTA, PTA, RTA = evaluate_template_level(df_groundtruth, df_parsedlog)

        result = {
            'Dataset': dataset,
            'ParseTime(s)': f"{parse_time:.2f}",
            'GA': f"{GA:.3f}",
            'FGA': f"{FGA:.3f}",
            'PA': f"{PA:.3f}",
            'PTA': f"{PTA:.3f}",
            'RTA': f"{RTA:.3f}",
            'FTA': f"{FTA:.3f}",
            'ToolTemplates': tool_templates,
            'GroundTemplates': ground_templates
        }
        all_results.append(result)
        print(f"Results for {dataset}: {result}")

    print(f"\n\n{'='*20} FINAL SUMMARY (AdvancedSimpleParser) {'='*20}")
    summary_df = pd.DataFrame(all_results)
    pd.set_option('display.max_rows', None)
    pd.set_option('display.max_columns', None)
    pd.set_option('display.width', None)
    pd.set_option('display.max_colwidth', None)
    print(summary_df.to_string())

if __name__ == '__main__':
    main()