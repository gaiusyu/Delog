import os
import shutil
import tarfile
import lzma
import random
import regex as re
from collections import defaultdict



def zigzag_encoder(num: int):
    return (num << 1) ^ (num >> 63)


def zigzag_decoder(num: int):
    return (num >> 1) ^ -(num & 1)

def elastic_encoder(num: int):
    buffer = b''
    cur = zigzag_encoder(num)
    while True:

        if cur < 0x80:
            buffer += cur.to_bytes(1, "little")
            break
        else:

            buffer += ((cur & 0x7f) | 0x80).to_bytes(1, 'little')
            cur >>= 7
    return buffer




def store_content_with_ids(input, output):
    content_to_id = {}
    id_to_content = {}
    id_counter = 1
    ids_file_path = 'Output/'  + str(
        output) + 'ids.bin'
    mapping_file_path = 'Output/'  + str(output) + 'mapping.txt'
    id_list = []
    if len(set(input))==1:
        content_to_id[input[0]] = 1
        with open(mapping_file_path, 'w', encoding="ISO-8859-1") as mapping_file:
            for content, content_id in content_to_id.items():
                mapping_file.write(f'{content}\n')

        return content_to_id

    for line in input:
        if isinstance(line, str):
            line = line.strip()
        if line != None:
            if line not in content_to_id:
                content_id = id_counter
                content_to_id[line] = content_id
                id_to_content[content_id] = line
                id_counter += 1
            else:
                content_id = content_to_id[line]
            id_list.append(content_id)
        else:
            id_list.append(0)
    with open(ids_file_path, 'wb') as ids_file, \
                open(mapping_file_path, 'w', encoding="ISO-8859-1") as mapping_file:
            for content, content_id in content_to_id.items():
                mapping_file.write(f'{content}\n')
            for id in id_list:
                ids_file.write(elastic_encoder(id))
    return content_to_id


def clear_directory(directory_path):

    if not os.path.exists(directory_path):
        os.makedirs(directory_path, exist_ok=True)

    for item in os.listdir(directory_path):
        item_path = os.path.join(directory_path, item)
        if os.path.isfile(item_path):
            os.remove(item_path)
        elif os.path.isdir(item_path):
            shutil.rmtree(item_path)


def replace_random_placeholder(input_string):

    placeholders = [match.start() for match in re.finditer(r'<\*.*?>', input_string)]

    if placeholders:
        selected_index = random.choice(placeholders)

        result = input_string[:selected_index] + '<selected>' + input_string[selected_index + 3:]
        return result
    else:
        return input_string

def replace_pattern(text):

    pattern = r'\*\{[^}]*\}'

    result = re.sub(pattern, '<*>', text)
    return result

def extract_variables_pure(logs, event_sequences):

    template_id_map = {}
    variable_groups = defaultdict(list)
    template_counter = 1
    id_list=[]
    event_list=[]

    for log, template in zip(logs, event_sequences):


        template = replace_pattern(template)



        template_parts = template.split()
        log_parts = log.split()
        if len(template_parts) != len(log_parts):
            event_list.append(log)
            continue
        digit_pattern = re.compile(r'\d')

        if template not in template_id_map:
            template_id = template_counter
            template_id_map[template] = template_counter
            template_counter += 1
        else:
            template_id = template_id_map[template]
        id_list.append(template_id)

        template_parts = template.split()

        for i, part in enumerate(template_parts):
            if "<*>" in part:
                group_key = f"{template_id}-{i + 1}"
                variable_groups[group_key].append(log_parts[i])
        event_list.append(template)

    template_id_mapping = {template: f"Event{template_id}" for template, template_id in template_id_map.items()}

    return variable_groups, template_id_mapping,template_id,event_list



def extract_variables(logs, event_sequences):

    template_id_map = {}
    variable_groups = defaultdict(list)
    template_counter = 1
    id_list=[]
    event_list=[]

    new_template_mapping = defaultdict(str)
    tag = 0
    count = 0
    new_e_seq=[]
    for log, template in zip(logs, event_sequences):



        if template not in new_template_mapping.keys():

            new_template_mapping[count] = template
            count+=1

    replace_mapping=defaultdict(str)
    while True:
        selected_count=random.randint(0, count-1)
        print("random seleced template id = " + str(selected_count) + '   '+str(count))
        seleted_template=new_template_mapping[selected_count]
        if "<*>" in seleted_template:
            break
        else:
            continue
    new_template=replace_random_placeholder(seleted_template)
    print("SELECTED TEMPLATE IS :" + new_template)
    replace_mapping[seleted_template]=new_template


    new_event_sequences=[]
    incorrect_GA_item=0
    for log, template in zip(logs, event_sequences):

        if template in replace_mapping.keys():
            template=replace_mapping[template]

        template_parts = template.split()
        log_parts = log.split()
        template = ''
        for t,l in zip(template_parts,log_parts):
            if "<selected>" in t:
                template=template+' '+l
                incorrect_GA_item+=1
            else:
                template = template + ' ' + t
        new_event_sequences.append(template)
    event_sequences=new_event_sequences
    print("INCORRECT_GA_ITEM = "+ str(incorrect_GA_item))

    template_id = 0
    for log, template in zip(logs, event_sequences):

        template_parts = template.split()
        log_parts = log.split()
        digit_pattern = re.compile(r'\d')

        count=0






        #
        # '''
        # PA=0
        # GA=1
        # '''
        # if template not in new_template_mapping.keys():
        #     oldtemplate=template
        #     template = ''
        #     for t in template_parts:
        #             if "<*>" not in t:
        #                 if tag==0 and random.choice([0, 1]) == 1:
        #                     tag=1
        #                     template = template + ' ' + "<*>"
        #                 else:
        #                     template=template+' '+t
        #             else:
        #                 template=template+' '+t
        #     new_template_mapping[oldtemplate]=template
        # else:
        #     template=new_template_mapping[template]
        #
        #
        # '''
        # PA=0
        # GA=1
        # '''
        # if template not in new_template_mapping.keys():
        #     oldtemplate=template
        #     template = ''
        #     for t in template_parts:
        #             if "<*>" not in t:
        #                 if tag!=4 and random.choice([0, 1]) == 1:
        #                     tag+=1
        #                     template = template + ' ' + "<*>"
        #                 else:
        #                     template=template+' '+t
        #             else:
        #                 template=template+' '+t
        #     new_template_mapping[oldtemplate]=template
        # else:
        #     template=new_template_mapping[template]
        #



        # if template == "jk2_init() Found child <*> in scoreboard slot <*>":
        #     template= "<*> Found child <*> in scoreboard slot <*>"
        # if template == "jk2_init() Can't find child <*> in scoreboard":
        #     template= "<*> Can't find child <*> in scoreboard"
        #     print("Asserted successful")
        # if template == "env.createBean2(): Factory error creating <*> (<*>, <*>)":
        #     template= "<*> Factory error creating <*> (<*>, <*>)"
        #     print("Asserted successful")
        # if template == "mod_jk2 Shutting down":
        #     template= "<*> Shutting down"
        #     print("Asserted successful")


        # if template == "[client <*>] File does not exist: <*>":
        #     new_t=''
        #     count=0
        #     for t,l in zip(template_parts,log_parts):
        #         if "<*>" in t:
        #             count+=1
        #             if count>=2:
        #                 new_t=new_t+' '+l
        #             else:
        #                 new_t = new_t + ' ' + t
        #         else:
        #             new_t=new_t+' '+t
        #     template=new_t

        if template not in template_id_map:
            template_id = template_counter
            template_id_map[template] = template_counter
            template_counter += 1
        else:
            template_id = template_id_map[template]
        id_list.append(template_id)

        template_parts = template.split()

        for i, part in enumerate(template_parts):

            if "<*>" in part:
                group_key = f"{template_id}-{i + 1}"
                variable_groups[group_key].append(log_parts[i])
        event_list.append(template)

    template_id_mapping = {template: f"Event{template_id}" for template, template_id in template_id_map.items()}

    return variable_groups, template_id_mapping,template_id,event_list

def delta_transform(num_list):
        initial = int(num_list[0])
        new_list = [initial]
        last = initial
        for item in num_list[1:]:
            delta = int(item) - int(last)
            new_list.append(delta)
            last = item
        return new_list


def variable_clustering(evnet, variables):
    variable_id_mapping=[]
    variable_dict=defaultdict(list)
    a=variables[4]
    variables=variables.tolist()
    for e,v_l in zip(evnet,variables):
        pos_id=0
        if v_l=="[]":
            continue
        if e not in variable_id_mapping:
            variable_id = len(variable_id_mapping)
            variable_id_mapping.append(e)
        else:
            variable_id=variable_id_mapping.index(e)
        s = v_l[2:-2]


        elements = s.split("', '")


        elements = [element.strip("'") for element in elements]
        for v in elements:
            varibale_tag=str(variable_id)+"_" + str(pos_id)
            variable_dict[varibale_tag].append(v)
            pos_id+=1
    return variable_dict

def compress_directory_to_lzma(directory_path, output_filename):

    if not os.path.exists(directory_path):
        raise FileNotFoundError(f"Dir not exist: {directory_path}")

    tar_filename = output_filename.replace(".lzma", ".tar")

    try:

        with tarfile.open(tar_filename, "w") as tar:
            tar.add(directory_path, arcname=os.path.basename(directory_path))
        print(f"Files Packed: {tar_filename}")

        with open(tar_filename, "rb") as tar_file:
            with lzma.open(output_filename, "wb") as lzma_file:
                lzma_file.write(tar_file.read())
        print(f"Compression Succussful: {output_filename}")

    finally:

        if os.path.exists(tar_filename):
            os.remove(tar_filename)
            print(f"Temp Files Deleted: {tar_filename}")


def get_file_size(file_path):
    if os.path.exists(file_path):
        file_size = os.path.getsize(file_path)
        return file_size
    else:
        return 0