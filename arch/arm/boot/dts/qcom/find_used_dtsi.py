#!/usr/bin/env python3

imports = []

def find_imports(path, depth=0):
  print(("|  "*depth) + path)
  imports.append(path)
  with open(path, 'r') as f:
    for line in f:
      if "#include" in line and "dts" in line:
        new_path = line.strip().split("\"")[1]
        find_imports(new_path, depth+1)

find_imports("msm8996pro-pmi8996-comma-leeco.dts")

print()
imports.sort()
for i in imports:
  print(i)
