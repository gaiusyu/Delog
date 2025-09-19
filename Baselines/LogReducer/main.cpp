#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string>
#include <set>
#include <ctime>
#include <string.h>
#include <regex>
#include <stdio.h>
#include <cstdio>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "template.h"
#include "constant.h"
#include <map>
#include "LengthSearch.h"
#include "elastic.h"
#include "util.h"

using namespace std;

//Log head
int HeadNum[MAXHEADN][MAXLOG];
string HeadFormat[MAXHEAD];

int HeadStrSize[MAXHEAD];
int HeadNumSize[MAXHEAD];

int HeadStrLen[MAXHEADN];
int HeadNumLen[MAXHEADN];

int HeadTemp[MAXHEADN];
int HeadSize[MAXHEADN];

int head_length;
int head_num_length;
int head_str_length;
int is_multi;
string headBound;
string HeadDic[MAXLOG];
map<string, int> head_mapping;
int head_dic_idx;

//Log data(Raw data, String data, Integer data)
int INTDATA[MAXCOL][MAXLOG];
string STRDATA[MAXCOL][MAXLOG];
string RAWDATA[MAXCOL][MAXLOG];

//Eid
int LINEID[MAXLOG];

//Buffer
char line[LINE_LENGTH];
char line2[LINE_LENGTH];
string linebuf[MAXTOCKEN];
map<int, int> template_counter;
int NumColumn[MAXCOL];
int StrColumn[MAXCOL];

//Mapping
map<string, int> num_mapping;
map<string, int> str_mapping;
map<string, int> idmap;

//Preprocess buffer
char *DELIM[128];

int encoder_type = 0;
bool diff = false; //Store diff
bool gDiff = false; //Store general diff
int DL = 0;
int corre_t = 600;

char *delim = (char *)" \t:=,";

void preprocess()
{
    for (int i = 0; i < 128; i++)
    {
        DELIM[i] = (char *)malloc(sizeof(char) * 2);
        DELIM[i][0] = (char)i;
        DELIM[i][1] = '\0';
    }
}

int loadTemplate(string input, LengthSearch *templateSearch)
{
    ifstream ifs;
    ifs.open(input.c_str());

    char *buf = NULL;
    int tid = 0;    //Template ID
    int fcount = 0; //Failed Line
    regex lineDelim("\\[(\\d+)\\]");
    bool start = true;
    int length = 0;
    bool success = true;
    int max_t = 0;
    int eid = 0;
    while (ifs.getline(line, LINE_LENGTH))
    {
        std::smatch m;
        string temp(line);
        bool found = regex_match(temp, m, lineDelim);
        template_counter[0] = 0;
        if (found)
        { //check delim line. a new template
            string p = m[1];
            eid = atoi(p.c_str());
            if (!start)
            { //not the first line
                if (success && length > 0) { // Only add if the previous template was valid
                    templateNode::sum++;
                    if (templateNode::sum > MAXTEMPLATE)
                    {
                        cout << "Exceed max template number" << endl;
                    }
                    else
                    {
                        template_counter[eid] = 0;
                        tid++;
                        cout << "add new node of E" << eid << endl;
                        templateSearch->addTemplate(eid, linebuf, length - 1, length - 1);
                        cout << "finish add" << endl;
                    }
                } else if (!success) {
                    cout << "Load Template Error at: E" << eid << " (previous template)" << endl;
                }
            }
            else
            { //The first line
                start = false;
            }
            length = 0;
            success = true;
        }
        else
        {
            if (!success)
            {
                continue;
            }
            int return_value = Mystrtok(line, delim, buf);
            while (buf != NULL or return_value != 0)
            {
                if (length > MAXTOCKEN - 4)
                {
                    success = false;
                    length = 0; //avoid length is too long
                    buf = NULL;
                    break;
                }
                if (buf)
                    linebuf[length++] = buf;
                linebuf[length++] = DELIM[return_value];
                return_value = Mystrtok(NULL, delim, buf);
            }

            linebuf[length++] = "\n"; //if multi line, add a \n,if not, \n will be moved at the start check
            if (!success)
            {
                fcount++;
            }
        }
    }
    // Process the very last template in the file
    if(success && length > 0 && !start) {
        templateNode::sum++;
        if (templateNode::sum <= MAXTEMPLATE) {
            template_counter[eid] = 0;
            tid++;
            cout << "add new node of E" << eid << endl;
            templateSearch->addTemplate(eid, linebuf, length-1, length-1);
            cout << "finish add" << endl;
        } else {
            cout << "Exceed max template number on the last template" << endl;
        }
    }
    cout << "read " << tid << " templates, failed " << fcount << endl;
    templateSearch->TemplatePrint();
    return tid;
}

void failProcess(FILE *fp, const char *failed_log, int length = -1)
{
    if (length == -1)
    {
        fprintf(fp, "%s\n", failed_log);
        return;
    }
    for (int i = 0; i < length; i++)
    {
        fputc((int)failed_log[i], fp);
    }
    fputc(10, fp);
}

int loadData(string input, string output_path, bool is_template, LengthSearch *templateSearch)
{
    char *buf;
    string totalBuf;
    int line_id = 0; //Line ID
    int lf_num = 0;  //Failed Line
    int mf_num = 0;  //match failed line

    //Failed reason
    int tot_too_long = 0;
    int wrong_format = 0;
    int no_content = 0;
    int too_many_token = 0;
    int too_many_variable = 0;
    int no_head = 0;
    int empty_line = 0;
    int bad_char = 0;

    char *head_delim = (char *)" \t|";
    regex headBound_r(headBound);
    bool readHead = true;
    bool success = true;
    int length = 0;
    int head = 0;

    templateNode *temp;
    bool start = true;

    string failed_log_path = output_path + "load_failed.log";
    FILE *ff = fopen(failed_log_path.c_str(), "w");
    string match_failed_path = output_path + "match_failed.log";
    FILE *mf = fopen(match_failed_path.c_str(), "w");
    if (ff == NULL)
    {
        cout << "Cannot open load_failed.log" << endl;
        exit(1);
    }
    if (mf == NULL)
    {
        cout << "Cannot open match_failed.log" << endl;
        exit(1);
    }
    int i = 0;
    int t_count = 0;

    FILE *pFile = fopen(input.c_str(), "r");
    if (pFile == NULL)
    {
        printf("Cannot open input file\n");
        exit(1);
    }
    fseek(pFile, 0, SEEK_END);
    long lSize = ftell(pFile);
    rewind(pFile);

    int nowSize = 0;
    while (nowSize < lSize)
    {
        int readSize = 0;
        int badChar = Myreadline(pFile, readSize, nowSize, lSize, line2);
        line2[readSize] = '\0';
        for (int i = 0; i <= readSize; i++)
        {
            line[i] = line2[i];
        }
        int return_value = 0;
        Mystrtok(line2, head_delim, buf); //For head
        if (strlen(line) == 0 || (is_multi && buf != NULL && regex_match(buf, headBound_r)) || (is_multi == 0))
        { //this is a new line,need to process the last line and reset
            if (!success)
            {
                lf_num++;
                failProcess(ff, totalBuf.c_str());
                if (line_id < MAXLOG) LINEID[line_id++] = -1;
                length = 0;
                head = 0;
                success = true;
                readHead = true;
                totalBuf = line;
            }
            else
            {
                if (!start)
                {
                    //match
                    temp = templateSearch->SearchTemplate(linebuf, length, length);
                    if (temp == NULL)
                    { //no match
                        failProcess(mf, totalBuf.c_str());
                        mf_num++;
                        if (line_id < MAXLOG) LINEID[line_id] = 0;
                        template_counter[0]++;
                    }
                    else
                    {
                        if (temp->paraLength >= MAXCOL)
                        { // ### FIX 1: Correct boundary check ###
                            too_many_variable++;
                            failProcess(ff, totalBuf.c_str());
                            lf_num++;
                            if (line_id < MAXLOG) LINEID[line_id] = -1;
                        }
                        else
                        { //Success read a line
                            //Load Head
                            for (int i = 0; i < head_num_length; i++)
                                if (HeadSize[i] < MAXLOG) HeadNum[i][HeadSize[i]++] = HeadTemp[i];
                            //Load Eid
                            int now_id = temp->Eid;
                            if (line_id < MAXLOG) LINEID[line_id] = now_id;
                            template_counter[now_id]++;

                            //Load Data
                            for (int j = 0; j < temp->paraLength; j++)
                            {
                                if (line_id < MAXLOG) RAWDATA[j][line_id] = linebuf[temp->stringIndex[j]];
                            }
                        }
                    }
                    line_id++;
                    //reset
                    length = 0;
                    head = 0;
                    success = true;
                    readHead = true;
                }
                else
                {
                    start = false;
                }
                totalBuf = line;
            }
            if (strlen(line) == 0)
            { //An empty line means a new start
                empty_line++;
                failProcess(ff, "", false);
                lf_num++;
                if (line_id < MAXLOG) LINEID[line_id++] = -1;
                start = true;
                continue;
            }

            if (badChar)
            { //badChar means a new start
                start = true;
                bad_char++;
                lf_num++;
                if (line_id < MAXLOG) LINEID[line_id++] = -1;
                failProcess(ff, line, readSize);
                continue;
            }
        }
        else
        {
            if (start)
            {
                no_head++;
                failProcess(ff, line);
                lf_num++;
                if (line_id < MAXLOG) LINEID[line_id++] = -1;
                continue;
            }
            totalBuf += '\n';
            totalBuf += line;
            if (!success)
            {
                continue;
            }
            readHead = false; //only the first line need to read linehead

            for (int i = 0; i <= readSize; i++)
            {
                line2[i] = line[i];
            }
            return_value = Mystrtok(line2, delim, buf); //For other line
            linebuf[length++] = "\n";
        }

        if (totalBuf.length() > MAX_LENGTH)
        {
            tot_too_long++;
            success = false;
        }

        //Read Head
        int now_num = 0;
        int now_str = 0;
        while (readHead && head < head_length && buf != NULL && success)
        {
            assert(now_num <= head_num_length);
            assert(now_str <= head_str_length);
            int num_tot = HeadNumSize[head];
            int str_tot = HeadStrSize[head];
            if (num_tot == 0)
            { //String head (Store in dictionary)
                string now_temp = buf;
                map<string, int>::iterator iit = head_mapping.find(now_temp);
                if (iit == head_mapping.end())
                { //Do not exist
                    head_mapping.insert(pair<string, int>(now_temp, head_dic_idx));
                    HeadDic[head_dic_idx] = now_temp;
                    HeadTemp[now_num] = head_dic_idx++;
                }
                else
                {
                    HeadTemp[now_num] = iit->second;
                }
                now_num++;
                now_str++;
            }
            else
            { //Num head (Extract number)
                int buf_pointer = 0;
                int format_pointer = 0;
                int tot_length = strlen(buf);
                while (buf_pointer < tot_length)
                {
                    if (format_pointer >= HeadFormat[head].length())
                    {
                        success = false;
                        break;
                    }
                    if (isdigit(buf[buf_pointer]) && HeadFormat[head][format_pointer] == '%')
                    { //Start a number
                        int readLength = 0;
                        int target = Myatoi(buf + buf_pointer, tot_length - buf_pointer, readLength);
                        if (readLength == -1 || (HeadNumLen[now_num] != -1 && HeadNumLen[now_num] != readLength))
                        {
                            success = false;
                            break;
                        }
                        buf_pointer += readLength;
                        format_pointer += 2;
                        HeadTemp[now_num] = target;
                        now_num++;
                    }
                    else
                    { //Start a string
                        for (int readLength = 0; readLength < HeadStrLen[now_str] && format_pointer < HeadFormat[head].length() && buf_pointer < tot_length; readLength++)
                        {
                            if (HeadFormat[head][format_pointer++] != (char)buf[buf_pointer++])
                            {
                                success = false;
                                break;
                            }
                        }
                        if (!success) break;
                        now_str++;
                    }
                }
                if (format_pointer != HeadFormat[head].length() || success == false)
                {
                    wrong_format++;
                    success = false;
                    break;
                }
            }
            head++;
            char *now_delim = (head == head_length) ? delim : head_delim;

            return_value = Mystrtok(NULL, now_delim, buf);
            while (buf == NULL and return_value != 0)
            {
                return_value = Mystrtok(NULL, now_delim, buf);
            }
            if (buf == NULL and return_value == 0)
            {
                no_content++;
                success = false;
            }
        }

        //Read Content
        while ((buf != NULL or return_value != 0) && success)
        {
            if (length + 2 >= MAXTOCKEN)
            {
                too_many_token++;
                success = false;
                buf = NULL;
                break;
            }
            if (buf)
                linebuf[length++] = buf;
            linebuf[length++] = DELIM[return_value];
            return_value = Mystrtok(NULL, delim, buf);
        }
    }

    // process the last line
    if (line_id < MAXLOG) { // Check before processing the last line
        if (success)
        {
            temp = templateSearch->SearchTemplate(linebuf, length, length);
            if (temp == NULL)
            { //no match
                int now_id = 0;
                LINEID[line_id] = now_id;
                for (int i = 0; i < length; i++)
                {
                    fprintf(mf, "%s", linebuf[i].c_str());
                }
                fprintf(mf, "\n");
                mf_num++;
                template_counter[now_id]++;
            }
            else
            {
                if (temp->paraLength >= MAXCOL)
                { // ### FIX 1: Also check for the last line ###
                    too_many_variable++;
                    failProcess(ff, totalBuf.c_str());
                    lf_num++;
                    LINEID[line_id] = -1;
                }
                else
                {
                    //Load Head
                    for (int i = 0; i < head_num_length; i++)
                    {
                        if(HeadSize[i] < MAXLOG) HeadNum[i][HeadSize[i]++] = HeadTemp[i];
                    }
                    int now_id = temp->Eid;
                    LINEID[line_id] = now_id;
                    template_counter[now_id]++;
                    for (int j = 0; j < temp->paraLength; j++)
                    {
                        RAWDATA[j][line_id] = linebuf[temp->stringIndex[j]];
                    }
                }
            }
            line_id++;
        }
        else
        {
            LINEID[line_id++] = -1;
            lf_num++;
            failProcess(ff, totalBuf.c_str());
        }
    }

    fclose(ff);
    fclose(mf);
    cout << "read logs: " << line_id << endl;
    cout << "load failed: " << lf_num << endl;
    printf("Fail Reason-> Total too long: %d, Wrong format: %d, No content: %d, Too many tockens: %d, Bad char: %d, Empty line: %d, Too many variables: %d, No head: %d\n", tot_too_long, wrong_format, no_content, too_many_token, bad_char, empty_line, too_many_variable, no_head);
    cout << "match failed: " << mf_num << endl;
    cout << "Load failed rate: " << (float)lf_num / (line_id) << endl;
    cout << "match failed rate: " << (float)mf_num / (line_id) << endl;
    return line_id;
}

bool variable_process(string template_path, string output_path, int data_num)
{
    map<int, int>::iterator iter;
    for (iter = template_counter.begin(); iter != template_counter.end(); iter++)
    {
        int eid = iter->first;
        if (iter->second == 0 || eid < 1)
            continue;

        string template_output_path = output_path + "E" + to_string(eid) + "_";
        num_mapping.clear();
        str_mapping.clear();
        idmap.clear();

        //Read basic rule
        FILE *basic = fopen((template_path + "E" + to_string(eid) + "basic.rule").c_str(), "r");
        if (basic == NULL)
        {
            cout << "No Basic with template " << to_string(eid) << endl;
            continue;
        }
        int tot, num_tot, str_tot;
        int log_size = 0;
        fscanf(basic, "%d%d%d", &tot, &num_tot, &str_tot);
        if (tot == 0)
        {
            fclose(basic);
            continue;
        }
        if (num_tot != 0)
        {
            for (int i = 0; i < num_tot; i++)
            {
                fscanf(basic, "%d", &NumColumn[i]);
                num_mapping[to_string(NumColumn[i])] = i;
            }
        }
        if (str_tot != 0)
        {
            for (int i = 0; i < str_tot; i++)
            {
                fscanf(basic, "%d", &StrColumn[i]);
                str_mapping[to_string(StrColumn[i])] = i;
            }
        }
        fclose(basic);

        //Fill up data
        for (int log = 0; log < data_num; log++)
        {
            if (log >= MAXLOG) break; // Safety break
            if (LINEID[log] != eid)
                continue;
            for (int i = 0; i < num_tot; i++)
            {
                int col = NumColumn[i];
                if (col < MAXCOL) INTDATA[i][log_size] = atoi(RAWDATA[col][log].c_str());
            }
            for (int i = 0; i < str_tot; i++)
            {
                int col = StrColumn[i];
                if (col < MAXCOL) STRDATA[i][log_size] = RAWDATA[col][log];
            }
            log_size++;
        }

        if (log_size == 0)
        {
            continue;
        }
        if (log_size < corre_t)
        {
            for (int i = 0; i < num_tot; i++)
                IntEncoder((char *)(template_output_path + to_string(NumColumn[i]) + ".dat").c_str(), (int *)INTDATA[i], log_size, encoder_type);
            for (int i = 0; i < str_tot; i++)
                StringEncoder((char *)(template_output_path + to_string(StrColumn[i]) + ".str").c_str(), STRDATA[i], log_size);
            continue;
        }
        int num_leer = num_tot;

        //Read and apply number rule
        if (num_tot > 0)
        {
            FILE *numrule = fopen((template_path + "E" + to_string(eid) + "num.rule").c_str(), "r");
            if (numrule == NULL)
            {
                cout << "No Number rule with template " << to_string(eid) << endl;
                return false;
            }
            char rule_temp[BUFSIZE];
            while (fscanf(numrule, "%s", &rule_temp) == 1)
            {
                string rule = rule_temp;
                if (rule == "direct")
                {
                    char a_c[BUFSIZE];
                    fscanf(numrule, "%s", &a_c);
                    string a = a_c;
                    int op = num_mapping[a];
                    IntEncoder((char *)(template_output_path + a + ".dat").c_str(), (int *)INTDATA[op], log_size, encoder_type);
                }

                if (rule == "doi" || rule == "upi")
                {
                    int opn, idn;
                    fscanf(numrule, "%d%d", &opn, &idn);
                    int op = num_mapping[to_string(opn)];
                    int id = str_mapping[to_string(idn)];
                    int nop = num_leer++;
                    if (nop >= MAXCOL)
                    {
                        cout << "Error! Exceeding max variables limit: " << MAXCOL;
                        return false;
                    }
                    idmap.clear();
                    if (rule == "doi") {
                        for (int i = 0; i < log_size; i++)
                        {
                            string now_id = STRDATA[id][i];
                            map<string, int>::iterator iit = idmap.find(now_id);
                            if (iit == idmap.end())
                            {
                                idmap.insert(pair<string, int>(now_id, i));
                                INTDATA[nop][i] = INTDATA[op][i];
                            }
                            else
                            {
                                INTDATA[nop][i] = INTDATA[op][i] - INTDATA[op][iit->second];
                                idmap[now_id] = i;
                            }
                        }
                        num_mapping[to_string(opn) + "_do_" + to_string(idn)] = nop;
                    } else { // upi
                        for (int i = log_size - 1; i >= 0; i--)
                        {
                            string now_id = STRDATA[id][i];
                            map<string, int>::iterator iit = idmap.find(now_id);
                            if (iit == idmap.end())
                            {
                                idmap.insert(pair<string, int>(now_id, i));
                                INTDATA[nop][i] = INTDATA[op][i];
                            }
                            else
                            {
                                INTDATA[nop][i] = INTDATA[op][iit->second] - INTDATA[op][i];
                                idmap[now_id] = i;
                            }
                        }
                        num_mapping[to_string(opn) + "_up_" + to_string(idn)] = nop;
                    }
                }

                if (rule == "do" || rule == "up")
                {
                    int opn;
                    fscanf(numrule, "%d", &opn);
                    int op = num_mapping[to_string(opn)];
                    int nop = num_leer++;
                    if (nop >= MAXCOL)
                    {
                        cout << "Error! Exceeding max variables limit: " << MAXCOL;
                        return false;
                    }
                    if (rule == "do") {
                        INTDATA[nop][0] = INTDATA[op][0];
                        for (int i = 1; i < log_size; i++)
                        {
                            INTDATA[nop][i] = INTDATA[op][i] - INTDATA[op][i - 1];
                        }
                        num_mapping[to_string(opn) + "_do"] = nop;
                    } else { // up
                        INTDATA[nop][log_size - 1] = INTDATA[op][log_size - 1];
                        for (int i = log_size - 2; i >= 0; i--)
                        {
                            INTDATA[nop][i] = INTDATA[op][i + 1] - INTDATA[op][i];
                        }
                        num_mapping[to_string(opn) + "_up"] = nop;
                    }
                }

                if (rule == "diff")
                {
                    int diff_count;
                    fscanf(numrule, "%d", &diff_count);
                    int nop = num_leer++;
                    if (nop >= MAXCOL)
                    {
                        cout << "Error! Exceeding max variables limit: " << MAXCOL;
                        return false;
                    }
                    string out_name = template_output_path;
                    if (diff_count == 2)
                    {
                        char a_c[BUFSIZE], b_c[BUFSIZE];
                        fscanf(numrule, "%s%s", &a_c, &b_c);
                        string a = a_c;
                        string b = b_c;
                        int op1 = num_mapping[a];
                        int op2 = num_mapping[b];
                        for (int i = 0; i < log_size; i++)
                        {
                            INTDATA[nop][i] = INTDATA[op1][i] - INTDATA[op2][i];
                        }
                        out_name += a + "-" + b + ".dat";
                    }
                    if (diff_count == 3)
                    {
                        char a_c[BUFSIZE], b_c[BUFSIZE], c_c[BUFSIZE];
                        fscanf(numrule, "%s%s%s", &a_c, &b_c, &c_c);
                        string a = a_c, b = b_c, c = c_c;
                        int op1 = num_mapping[a];
                        int op2 = num_mapping[b];
                        int op3 = num_mapping[c];
                        for (int i = 0; i < log_size; i++)
                        {
                            INTDATA[nop][i] = INTDATA[op1][i] - INTDATA[op2][i] - INTDATA[op3][i];
                        }
                        out_name += a + "-" + b + "-" + c + ".dat";
                    }
                    IntEncoder((char *)out_name.c_str(), (int *)INTDATA[nop], log_size, encoder_type);
                }
            }
            fclose(numrule);
        }
        
        //Read and apply string rule
        if (str_tot != 0)
        {
            FILE *strrule = fopen((template_path + "E" + to_string(eid) + "string.rule").c_str(), "r");
            if (strrule == NULL)
            {
                cout << "No Str rule with template " << to_string(eid) << endl;
                return false;
            }
            char str_op[BUFSIZE];
            while (fscanf(strrule, "%s", &str_op) == 1)
            {
                string rule = str_op;
                if (rule == "direct")
                {
                    char pn[BUFSIZE];
                    fscanf(strrule, "%s", &pn);
                    string spn = pn;
                    int op = str_mapping[spn];
                    string dic_out = template_output_path + spn + ".str";
                    StringEncoder((char *)dic_out.c_str(), STRDATA[op], log_size);
                }
            }
            fclose(strrule);
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    int o;
    const char *optstring = "HhI:O:T:D:E:X:Y:B:F:";

    string input_path;
    string template_path;
    string output_path;
    string type;
    string diff_mode;
    string encode_mode;

    string block_size;
    int BLOCKSIZE;
    string head_format;

    string fileStartNo;
    string fileEndNo;
    while ((o = getopt(argc, argv, optstring)) != -1)
    {
        switch (o)
        {
        case 'I':
            input_path = optarg;
            printf("input file path: %s\n", input_path.c_str());
            break;
        case 'X':
            fileStartNo = optarg;
            printf("input file start no: %s\n", fileStartNo.c_str());
            break;
        case 'Y':
            fileEndNo = optarg;
            printf("input file end no: %s\n", fileEndNo.c_str());
            break;
        case 'O':
            output_path = optarg;
            printf("output path : %s\n", output_path.c_str());
            break;
        case 'T':
            template_path = optarg;
            printf("templates file :%s\n", template_path.c_str());
            break;
        case 'D':
            diff_mode = optarg;
            printf("Diff Mode is: %s\n", diff_mode.c_str());
            break;
        case 'E':
            encode_mode = optarg;
            printf("Encode Mode is: %s\n", encode_mode.c_str());
            break;
        case 'B':
            block_size = optarg;
            printf("Block size is :%s\n", block_size.c_str());
            break;
        case 'F':
            head_format = optarg;
            printf("Head format path is:%s\n", head_format.c_str());
            break;
        case 'h':
        case 'H':
            printf("-I input path\n");
            printf("-X input file start no\n");
            printf("-Y input file end no\n");
            printf("-O output path\n");
            printf("-S template path\n");
            printf("-P match policy\n");
            printf("-D time delta mode\n");
            printf("-E encode mode\n");
            printf("-G general delta mo/de\n");
            return 0;
            break;
        case '?':
            printf("error:wrong opt!\n");
            printf("error optopt: %c\n", optopt);
            printf("error opterr: %c\n", opterr);
            return 1;
        }
    }

    if (input_path == "")
    {
        printf("error : No input file\n");
        return -1;
    }
    if (output_path == "")
    {
        printf("error : No output\n");
        return -1;
    }
    if (template_path == "")
    {
        printf("error : No templates\n");
        return -1;
    }

    if (type == "")
    {
        type = "Noknown";
    }
    if (diff_mode == "")
    {
        diff_mode = "D";
    }
    if (encode_mode == "")
    {
        encode_mode = "Z";
    }

    if (block_size == "")
    {
        BLOCKSIZE = 100000;
    }
    else
    {
        BLOCKSIZE = atoi(block_size.c_str());
    }

    if (head_format == "")
    {
        printf("Error: No head format message\n");
    }

    if (fileStartNo == "" || fileEndNo == "")
    {
        printf("Do not know file number\n");
        return -1;
    }

    diff = (diff_mode == "D") ? true : false;
    encoder_type = (encode_mode == "Z") ? 1 : 0;

    FILE *hf = fopen(head_format.c_str(), "r");
    if (hf == NULL)
    {
        printf("Head format open failed\n");
        return -1;
    }
    fscanf(hf, "%d", &head_length);
    fscanf(hf, "%d", &is_multi);
    if (is_multi)
    {
        char btemp[BUFSIZE];
        fscanf(hf, "%s", &btemp);
        headBound = btemp;
    }
    if (head_length < 0 || head_length > MAXHEAD)
    {
        printf("Invalid head length(<0 or >MAXHEAD(16)");
        return -1;
    }
    int now_str = 0;
    int now_num = 0;
    for (int i = 0; i < head_length; i++)
    {
        fscanf(hf, "%d%d", &HeadStrSize[i], &HeadNumSize[i]);
        head_str_length += HeadStrSize[i];
        head_num_length += HeadNumSize[i];
        if (HeadStrSize[i] == 1 && HeadNumSize[i] == 0)
        {
            head_num_length++;
            now_num++;
        }
        char temp[BUFSIZE];
        fscanf(hf, "%s", &temp);
        HeadFormat[i] = temp;

        for (int t = 0; t < HeadStrSize[i]; t++)
        {
            fscanf(hf, "%d", &HeadStrLen[now_str++]);
        }
        for (int t = 0; t < HeadNumSize[i]; t++)
        {
            fscanf(hf, "%d", &HeadNumLen[now_num++]);
        }
    }
    fclose(hf);

    preprocess();

    LengthSearch *templateSearch = new LengthSearch();

    int max_template = loadTemplate(template_path + "template.col", templateSearch);
    for (int i = atoi(fileStartNo.c_str()); i <= atoi(fileEndNo.c_str()); i++)
    {
        for (int k = 0; k < head_num_length; k++) HeadTemp[k] = -1;
        for (int k = 0; k < head_num_length; k++) HeadSize[k] = 0;

        string prename = (const char *)(to_string(i)).c_str();
        string filename = (const char *)(to_string(i) + ".col").c_str();
        string tempFilePath = output_path + prename + "/";
        
        if (access(tempFilePath.c_str(), 0) == -1)
        {
            int isCreate = mkdir(tempFilePath.c_str(), 0755);
            if (isCreate != 0) // ### FIX 2: Correct check for mkdir ###
                printf("error : create new file:%s failed.\n", tempFilePath.c_str());
        }
        printf("load data start... of ");
        cout << input_path + filename << endl;
        clock_t start = clock();
        int data_num = loadData(input_path + filename, tempFilePath, false, templateSearch);
        clock_t end = clock();
        printf("It takes %lfs to load template and data\n", (double)(end - start) / CLOCKS_PER_SEC);

        //Variables processing
        printf("variable process start...\n");
        bool result = variable_process(template_path, tempFilePath, data_num);
        clock_t end2 = clock();
        printf("It takes %lfs to process variable\n", (double)(end2 - end) / CLOCKS_PER_SEC);
        if (result != true)
        {
            return 1;
        }

        //Output
        if (HeadSize[0] > 0) printf("ReadHead: %d\n", HeadSize[0]);
        IntEncoder((const char *)(tempFilePath + "Eid" + ".eid").c_str(), LINEID, data_num, encoder_type);
        for (int j = 0; j < head_num_length; j++)
        {
            IntEncoder((const char *)(tempFilePath + "Head" + to_string(j) + ".head").c_str(), HeadNum[j], HeadSize[j], encoder_type, diff);
        }
        StringEncoder((const char *)(tempFilePath + "Header_dictionary" + ".headDict").c_str(), HeadDic, head_dic_idx);
    }

    map<int, int>::iterator iter;
    for (iter = template_counter.begin(); iter != template_counter.end(); iter++)
    {
        printf("E%d:%d\n", iter->first, iter->second);
    }
    return 0;
}