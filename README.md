#### autotox 
This is based on minitox <br />
Need package libtoxcore-dev, install by running: sudo apt install libtoxcore-dev <br />
Make a autotox for remote survey on /var/lib which is local machine directory <br />
Step1: Local site: create /var/lib and /var/lib/share and /var/lib/backup <br />
Step2: Local site: go to autotox folder,run: make clean and then run: make <br />
Step3: Local site: start autotox, go to autotox folder, run: ./autotox <br />
Step4: Remote site: using a Tox client at remote side, add autotox as friend: add autotox'ID and provide message needed for adding - message default: autotox (both are displayed on the screen after starting autotox at local site) <br />
Step5: Remote site: enter command for autotox friend at remote site's Tox client: cmd -> waiting autotox's response that provide all commands. <br />
Step6: Ok, using these commands to survey /var/lib residing at local site <br />
That's all, just for fun!

