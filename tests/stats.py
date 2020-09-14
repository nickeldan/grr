#!/usr/bin/python3

import collections

if __name__ == '__main__':
    d=collections.defaultdict(list)
    with open('stats','r') as f:
        for line in f:
            if line.startswith('user'):
                user=float(line.split('m')[1].split('s')[0])
                d['user'].append(user)
            elif line.startswith('sys'):
                sys=float(line.split('m')[1].split('s')[0])
                d['sys'].append(sys)

    for key,value in d.items():
        print(key,sum(value)/len(value))
