# evaluation_logic.py

import pandas as pd
from tqdm import tqdm

def get_group_accuracy(series_groundtruth, series_parsedlog):
    series_parsedlog_valuecounts = series_parsedlog.value_counts()
    accurate_events = 0

    for parsed_eventId in series_parsedlog_valuecounts.index:
        logIds = series_parsedlog[series_parsedlog == parsed_eventId].index
        series_groundtruth_logId_valuecounts = series_groundtruth[logIds].value_counts()
        
        if series_groundtruth_logId_valuecounts.size == 1:
            groundtruth_eventId = series_groundtruth_logId_valuecounts.index[0]
            if logIds.size == series_groundtruth[series_groundtruth == groundtruth_eventId].size:
                accurate_events += logIds.size

    GA = float(accurate_events) / series_groundtruth.size if series_groundtruth.size > 0 else 0
    
    true_pairs = 0
    pred_pairs = 0
    true_pred_pairs = 0

    for group_val in series_groundtruth.value_counts().values:
        true_pairs += group_val * (group_val - 1) / 2
    
    for group_val in series_parsedlog.value_counts().values:
        pred_pairs += group_val * (group_val - 1) / 2

    df_combined = pd.concat([series_groundtruth, series_parsedlog], axis=1, keys=['ground', 'pred'])
    for _, group in df_combined.groupby(['ground', 'pred']):
        count = len(group)
        if count > 1:
            true_pred_pairs += count * (count - 1) / 2
    
    precision = true_pred_pairs / pred_pairs if pred_pairs > 0 else 0
    recall = true_pred_pairs / true_pairs if true_pairs > 0 else 0
    FGA = 2 * precision * recall / (precision + recall) if (precision + recall) > 0 else 0

    return GA, FGA

def evaluate_template_level(df_groundtruth, df_parsedresult):
    null_logids = df_groundtruth[~df_groundtruth['EventTemplate'].isnull()].index
    df_groundtruth = df_groundtruth.loc[null_logids]
    df_parsedresult = df_parsedresult.loc[null_logids]

    series_groundtruth = df_groundtruth['EventTemplate']
    series_parsedlog = df_parsedresult['EventTemplate']
    
    groundtruth_templates = series_groundtruth.unique()
    parsed_templates = series_parsedlog.unique()

    df_combined = pd.concat([series_groundtruth, series_parsedlog], axis=1, keys=['groundtruth', 'parsedlog'])
    grouped_df = df_combined.groupby('parsedlog')
    
    correct_parsing_templates = 0
    for identified_template, group in grouped_df:
        corr_oracle_templates = set(list(group['groundtruth']))
        
        if corr_oracle_templates == {identified_template}:
            correct_parsing_templates += 1

    num_parsed_templates = len(parsed_templates)
    num_groundtruth_templates = len(groundtruth_templates)

    PTA = correct_parsing_templates / num_parsed_templates if num_parsed_templates > 0 else 0
    RTA = correct_parsing_templates / num_groundtruth_templates if num_groundtruth_templates > 0 else 0
    
    FTA = 0.0
    if PTA > 0 or RTA > 0:
        FTA = 2 * (PTA * RTA) / (PTA + RTA)

    return num_parsed_templates, num_groundtruth_templates, FTA, PTA, RTA