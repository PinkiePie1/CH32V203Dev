pincount = 16
for i in range(pincount-1):
    for j in range(i+1,0,-1):
        print(pincount-j+i*pincount,end = "")
        print(",",end = "")
    for j in range(pincount-i-1):
        print(j+i*pincount,end = "")
        print(",",end = "")
    print("\r\n")

        
