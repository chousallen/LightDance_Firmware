import struct

def calculate_checksum(data):
    return sum(data) & 0xFFFFFFFF

def read_control_file():
    with open("control.dat", "rb") as file:
        data = file.read()
        file_size = len(data)
        
        control_data = data[:-4]
        stored_checksum = struct.unpack_from('<I', data, file_size - 4)[0]
        
        offset = 0
        version = control_data[0:2]
        offset += 2
        
        print("=== control.dat ===")
        print(f"Version: {version[0]}.{version[1]}")
        
        of_channel = []
        for i in range(40):
            of_channel.append(control_data[offset])
            offset += 1
        
        strip_channel = []
        for i in range(8):
            strip_channel.append(control_data[offset])
            offset += 1
        
        frame_num = struct.unpack_from('<I', control_data, offset)[0]
        offset += 4
        
        timestamps = []
        for i in range(frame_num):
            timestamps.append(struct.unpack_from('<I', control_data, offset)[0])
            offset += 4
        
        # 驗證 checksum
        calc_checksum = calculate_checksum(control_data)
        checksum_ok = (calc_checksum == stored_checksum)
        
        print(f"Enabled OF: {sum(of_channel)}")
        print(f"Enabled Strip: {sum(1 for x in strip_channel if x > 0)}")
        print(f"Total LED: {sum(strip_channel)}")
        print(f"Frame num: {frame_num}")
        print(f"File size: {file_size} bytes")
        print(f"Checksum: {stored_checksum:08X} ({'OK' if checksum_ok else 'ERROR'})")
        
        return version, of_channel, strip_channel, frame_num

def read_frame_file(of_channel, strip_channel, frame_num):
    with open("frame.dat", "rb") as file:
        version = file.read(2)
        
        print("\n=== frame.dat ===")
        print(f"Version: {version[0]}.{version[1]}")
        
        of_num = sum(of_channel)
        total_leds = sum(strip_channel)
        frame_size = 4 + 1 + (of_num * 3) + (total_leds * 3) + 4
        
        print(f"Frame size: {frame_size} bytes")
        
        frame_count = 0
        while True:  # 改為使用已知的 frame_num
            frame_data = file.read(frame_size)
            if len(frame_data) != frame_size:
                break
            
            tester = 0
            
            offset = 0
            start_time = struct.unpack_from('<I', frame_data, offset)[0]
            offset += 4
            
            fade = frame_data[offset]
            offset += 1
            
            print(f"\nFrame{frame_count}: time={start_time}, fade={'on' if fade else 'off'}")
            
            for i, enabled in enumerate(of_channel):
                if enabled:
                    g = frame_data[offset]
                    r = frame_data[offset + 1]
                    b = frame_data[offset + 2]
                    offset += 3
                    tester = tester + r + g + b
                    print(f"  OF[{i}]: G={g:03d}, R={r:03d}, B={b:03d}")
            
            for i, strip_leds in enumerate(strip_channel):
                if strip_leds > 0:
                    for j in range(strip_leds):
                        g = frame_data[offset]
                        r = frame_data[offset + 1]
                        b = frame_data[offset + 2]
                        offset += 3
                        tester = tester + r + g + b
                        print(f"  LED[{i}][{j}]: G={g:03d}, R={r:03d}, B={b:03d}")
            
            stored_checksum = struct.unpack_from('<I', frame_data, offset)[0]
            print(f"  Checksum: {stored_checksum:08X}")
            
            frame_count += 1
            
            if tester != 0:
                input("按 Enter 鍵繼續下一幀...")
        
        print(f"\nTotal frames read: {frame_count}")

version, of_channel, strip_channel, frame_num = read_control_file()
  
print("\nRead frame.dat? (y/n): ")
if input().strip().lower() == 'y':
    read_frame_file(of_channel, strip_channel, frame_num)