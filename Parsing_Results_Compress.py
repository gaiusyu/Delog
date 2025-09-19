import os

from Compressor import compression_tools
import pandas as pd

input_dir = "Loghub_data/"  # The input directory of log file
output_dir = "Drain_result/"  # The output directory of parsing results
logname="Apache"
indir=input_dir+logname+'/'

groundtruth=indir+"/"+logname + "_full.log_structured.csv"
df_groundtruth = pd.read_csv(groundtruth,encoding="ISO-8859-1", header=0)

templates_path=indir+"/"+logname + "_2k.log_templates.csv"
df_templates = pd.read_csv(templates_path,encoding="ISO-8859-1", header=0)
mapping_eventid=df_templates["EventId"]
mapping_event=df_templates["EventTemplate"]

compression_tools.clear_directory("Output")

'''
STEP1:  Store log event sequences
'''
Event_sequences= df_groundtruth["EventTemplate"]

#compression_tools.store_content_with_ids(Event_sequences,"Events")

'''
STEP1:  Store Variable
'''
Logs = df_groundtruth["Content"]



# 执行提取任务
result,mapiing,template_id,event_list = compression_tools.extract_variables(Logs, Event_sequences)
compression_tools.store_content_with_ids(event_list,"Events")
#os.remove("Output/Eventsmapping.txt")
for key in result.keys():
    path="Output/"+key

    variable_list = result[key]
    num_tag=True
    for word in variable_list:
        if not word.isdigit():
            num_tag=False
            break
    '''
    Numbers are processed differently from other variables
    '''
    if num_tag == True:
        save_tmp=compression_tools.delta_transform(variable_list)
        filename=path+".bin"
        with open(filename, 'ab') as file:
            for word in save_tmp:
                file.write(compression_tools.elastic_encoder(int(word)))
    else:
        compression_tools.store_content_with_ids(variable_list,key)

# 打印结果
'''
STEP3: Store Log Headers
'''
for key in df_groundtruth.keys():
    if key not in ["EventId", "Content", "EventTemplate","LineId"]:
        path="Output/"+key
        variable_list = df_groundtruth[key]
        num_tag=True
        for word in variable_list:
            if not word.isdigit():
                num_tag=False
                break
        '''
        Numbers are processed differently from other variables
        '''
        if num_tag == True:
            save_tmp=compression_tools.delta_transform(variable_list)
            filename=path+".bin"
            with open(filename, 'ab') as file:
                for word in save_tmp:
                    file.write(compression_tools.elastic_encoder(int(word)))
        else:
            compression_tools.store_content_with_ids(variable_list,key)


'''
STEP4: General-compress
'''

compression_tools.compress_directory_to_lzma("Output",logname+".lzma")

for idx, group in enumerate(result):
    print(f"Group {idx}: {group}")

size=compression_tools.get_file_size(logname+".lzma")


print("Achieved size = " + str(size))
