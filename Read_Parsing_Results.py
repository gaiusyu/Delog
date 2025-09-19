import time

from Compressor import compression_tools
import pandas as pd
origin_file_path="Loghub_data/"
input_dir = "Drain_result/"  # The input directory of log file
logname="Apache"
indir=input_dir+logname+'/'
start_time = time.perf_counter()
groundtruth=indir+"/"+logname + "_full.log_structured.csv"
df_groundtruth = pd.read_csv(groundtruth,encoding="ISO-8859-1", header=0)

compression_tools.clear_directory("Output")

'''
STEP1:  Store log event sequences
'''
Event_sequences= df_groundtruth["EventTemplate"]

variables= df_groundtruth["ParameterList"]

variable_dict=compression_tools.variable_clustering(Event_sequences,variables)
#compression_tools.store_content_with_ids(Event_sequences,"Events")

'''
STEP1:  Store Variable
'''
Logs = df_groundtruth["Content"]



# 执行提取任务
#result,mapiing,template_id,event_list = compression_tools.extract_variables_pure(Logs, Event_sequences)
compression_tools.store_content_with_ids(Event_sequences,"Events")
#os.remove("Output/Eventsmapping.txt")
for key in variable_dict.keys():
    path="Output/"+key
    variable_list = variable_dict[key]
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
    if key not in ["EventId", "Content", "EventTemplate","LineId","ParameterList"]:
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

# for idx, group in enumerate(result):
#     print(f"Group {idx}: {group}")

size=compression_tools.get_file_size(logname+".lzma")
oringin_size=compression_tools.get_file_size(origin_file_path+"/"+logname+'/'+logname+'_2k.log')
end_time = time.perf_counter()
print("oringin size = " + str(oringin_size))
print("Achieved size = " + str(size))
print("compression ratio = " + str(oringin_size / size))
execution_time_ms = (end_time - start_time) * 1000
print(f"Execution time: {execution_time_ms:.2f} ms")