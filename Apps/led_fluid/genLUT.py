pincount = 16
for i in range(pincount-1):
    for j in range(i+1):
        print(pincount-j-1+i*pincount,end = "")
        print(",",end = "")
    for j in range(pincount-i-1):
        print(j+i*pincount,end = "")
        print(",",end = "")
    print("\r\n")

        
