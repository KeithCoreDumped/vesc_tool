import glob

with open("./res_fw.qrc","wt",encoding="utf-8") as f:
    f.write("<RCC>\n")
    f.write("\t<qresource prefix=\"/\">\n")
    for x in glob.glob("./*/*.bin"):
        x = x.replace('\\','/')
        f.write(f"\t\t<file>D:\\WorkLoad\\Robotics\\VESC\\vesc_tool\\res/firmwares/{x[2:]}</file>\n")
    f.write("\t</qresource>\n")
    f.write("</RCC>")
    