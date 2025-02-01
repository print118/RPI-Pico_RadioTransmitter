# RPI-Pico_RadioTransmitter
This Arduino program is able to get a Raspberry Pi Pico to transmit a simple carrier wave. Similar projects have existed for the Pico board as well, but to my knowledge they seem to work at only lower frequencies. Of course they can output audio compared to just a carrier wave so there's that. 

## How it works

### Basics
The script first checks to see what frequency you've selected to see how to transmit on said frequency. The basic idea for producing the carrier wave, is taking advantage of the Pico's feature to output the processor clock to a GPIO Pin. This allows to produce a square wave at the same frequency as the clock rate. The clock rate is usually about 125MHz, but with overclocking I was able to bring it to 300MHz and remain stable. 

### Other frequencies
#### Below 300MHz
In case the clock frequency can't directly match the wanted transmit frequency, the Pico offers a way to divide the output GPIO clock signal with two dividers. They are quite granular however so small changes in frequency, especially closer to 300MHz, start to get difficult. Usually the code does get very close however, but it requires some processing power to calculate the optimal ratio of clock frequency and the dividers. For this reason it also overclocks the processor when running the calculations in order to speed it up. 

#### Above 300MHz
The code can also transmit at frequencies higher than 300MHz, but it becomes quite unreliable and is currently pretty experimental. It works by taking advantage of the harmonics of the square wave, which appear at integer multiple intervals of the base frequency. I.e 2 x basefreq, 3 x basefreq and so on. Harmonics are usually bad and not very clean so this isn't the best method for transmitting at higher frequencies. They also cause problems with interference to other radio devices, which is unwanted. 

## Safety
Since this code does allow the Pico to transmit radio frequencies, it may be subject to litigation in your country. It is suggested to not use an antenna to limit the distance and to include a low-pass filter to filter out unwanted harmonics. 
