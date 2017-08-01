import numpy as np
import scipy.io
import socket
import os

import matplotlib
from matplotlib import gridspec
import matplotlib.pyplot as plt
import matplotlib.patches as pat
import matplotlib.animation as animation

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

import sys
if sys.version_info[0] < 3:
    import Tkinter as Tk
else:
    import tkinter as Tk

matplotlib.use('TkAgg')

class DraggableRectangle:
    def __init__(self, rect, ax, fig):
        self.rect = rect
        self.press = None
        self.resize_amt = 5
        self.ax  = ax
        self.fig = fig

    def connect(self):
        'connect to all the events we need'
        self.cidpress = self.rect.figure.canvas.mpl_connect(
            'button_press_event', self.on_press)
        self.cidrelease = self.rect.figure.canvas.mpl_connect(
            'button_release_event', self.on_release)
        self.cidmotion = self.rect.figure.canvas.mpl_connect(
            'motion_notify_event', self.on_motion)
        self.cidincrease = self.rect.figure.canvas.mpl_connect(
            'key_press_event', self.keypress)

    def on_press(self, event):
        self.fig.canvas.get_tk_widget().focus_force()
        'on button press we will see if the mouse is over us and store some data'
        if event.inaxes != self.rect.axes: return

        contains, attrd = self.rect.contains(event)
        if not contains: return
        print('event contains', self.rect.xy)
        x0, y0 = self.rect.xy
        self.press = x0, y0, event.xdata, event.ydata

    def keypress(self, event):
        if event.key == "right":
            self.increase_window(event)
        elif event.key == "left":
            self.decrease_window(event)
        self.rect.figure.canvas.draw()

    def increase_window(self, event):
        w = self.rect.get_width()
        self.rect.set_width(w+self.resize_amt)
        self.resize_ax()

    def resize_ax(self):
        x0,y0 = self.rect.xy
        x1 = self.rect.get_width()
        self.ax.set_xlim(left=x0, right=x0+x1)

    def decrease_window(self, event):
        w = self.rect.get_width()
        self.rect.set_width(w-self.resize_amt)
        self.resize_ax()

    def on_motion(self, event):
        'on motion we will move the rect if the mouse is over us'
        if self.press is None: return
        if event.inaxes != self.rect.axes: return
        x0, y0, xpress, ypress = self.press

        dx = event.xdata - xpress
        dy = event.ydata - ypress
        #print('x0=%f, xpress=%f, event.xdata=%f, dx=%f, x0+dx=%f' %
        #      (x0, xpress, event.xdata, dx, x0+dx))
        self.rect.set_x(x0+dx)
        # self.rect.set_y(y0+dy)
        self.resize_ax()
        self.rect.figure.canvas.draw()

    def on_release(self, event):
        'on release we reset the press data'
        self.press = None
        self.rect.figure.canvas.draw()

    def disconnect(self):
        'disconnect all the stored connection ids'
        self.rect.figure.canvas.mpl_disconnect(self.cidpress)
        self.rect.figure.canvas.mpl_disconnect(self.cidrelease)
        self.rect.figure.canvas.mpl_disconnect(self.cidmotion)


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server_address = ('localhost', 7377)
offset = 30.97 - 30
colors = ["#4542f4", "#41f465", "#f44141", "#f441e5"]

def load_matfile(filename):
    return scipy.io.loadmat(filename)['sdata'][0][0][1]

def draw_rects(data, ax, bottom):
    values = data[:, 2].astype(int)
    for i in range(0, np.size(data[:, 0])):
        dur = data[i, 1] - data[i, 0]
        if dur > 0:
            ax.add_patch(pat.Rectangle((data[i, 0], bottom), dur, 10, color=colors[values[i] - 1]))

def onclick(event, axID):
    if event.inaxes == axID:
        print('button=%d, x=%d, y=%d, xdata=%f, ydata=%f' %
              (event.button, event.x, event.y, event.xdata, event.ydata))
        command = "seekto " + str(event.xdata + offset)
        sock.sendto(command.encode(), server_address)

def pausevideo():
    command = "pause"
    sock.sendto(command.encode(), server_address)

def playvideo():
    command = "play"
    sock.sendto(command.encode(), server_address)

def openvideos():
    command = "open c:/users/sbf/Desktop/cam01.mov"
    sock.sendto(command.encode(), server_address)
    command = "open c:/users/sbf/Desktop/cam02.mov"
    sock.sendto(command.encode(), server_address)

def destroy():
    command = "break"
    sock.sendto(command.encode(), server_address)
    root.quit()
    root.destroy()


def add_variable(filename, ax, axc, axname):
    print(filename)
    bot, top = ax.get_ylim()
    data = load_matfile("derived/" + filename)
    draw_rects(data, ax, top)
    if bot < 0:
        ax.set_ylim(bottom=0, top=10.5)
        axname.set_ylim(bottom=0, top=10.5)
        top = 10.5
    else:
        ax.set_ylim(top=top+10.5)
        axname.set_ylim(top=top+10.5)
        top = top + 10.5
    ax.set_xlim(left=data[0, 0], right=data[-1, 1])
    axc.set_xlim(left=data[0, 0], right=data[-1, 1])
    axname.add_patch(pat.Rectangle((0,top-8), 1, 4, color="orange"))
    axname.text(0,top-6.5, filename)
    ax.figure.canvas.draw()

root = Tk.Tk()
root.wm_title("Embedding in TK")
window = Tk.Toplevel(master=root)
windowLEFT = Tk.Frame(master=window, bg="red")
windowRIGHT = Tk.Frame(master=window, bg="green")
rootTOP = Tk.Frame(master=root)
rootBOT = Tk.Frame(master=root)

plt.close('all')

# Just a figure and one subplot

f = plt.figure(figsize=(8,3))
gs = gridspec.GridSpec(2,2, width_ratios = [10,1], height_ratios = [5,1])
gs.update(left=0.01, right=0.99, wspace=0.01)
ax = plt.subplot(gs[0,0])

axname = plt.subplot(gs[0,1])
axname.set_ylim(bottom=0, top=10.5)
axname.set_xlim(left=0, right=1)
axname.axis('off')


ax.set_title('Simple plot')
ax.set_ylim(bottom=-1, top=0)
ax.set_yticklabels([])
ax.set_yticks([])

axc = plt.subplot(gs[1,0])
axc.set_ylim(bottom=0, top=10)
axc.set_yticklabels([])
axc.set_yticks([])

add_variable("cevent_eye_roi_child.mat", ax, axc, axname)


ctrl_bar = axc.add_patch(pat.Rectangle((30, 0), 10, 10, color=colors[1]))
dr = DraggableRectangle(ctrl_bar, ax, f)

ctrl_marker = axc.add_patch(pat.Rectangle((50, 0), 2, 2, color="red"))

def update_marker(event):
    command = "gettime"
    # sock.sendto(command.encode(), server_address)
    # rec = sock.recv(20)
    # rec = float(rec)
    x = ctrl_marker.get_x()
    ctrl_marker.set_x(x+0.1)

# a tk.DrawingArea
canvas = FigureCanvasTkAgg(f, master=rootTOP)
canvas.show()
# canvas.get_tk_widget().pack(side=Tk.LEFT, fill=Tk.BOTH, expand=1)

canvas._tkcanvas.pack(side=Tk.TOP, fill=Tk.BOTH, expand=1)
canvas.mpl_connect('button_press_event', lambda event: onclick(event, ax))
dr.connect()

buttonQuit = Tk.Button(master=rootBOT, text='Quit', command = destroy)
buttonQuit.pack(side=Tk.LEFT)

buttonPlay = Tk.Button(master=rootBOT, text='Play', command = playvideo).pack(side=Tk.LEFT)
buttonPause = Tk.Button(master=rootBOT, text='Pause', command = pausevideo).pack(side=Tk.LEFT)
buttonOpenvideos = Tk.Button(master=rootBOT, text='OpenVideos', command = openvideos).pack(side=Tk.LEFT)

# File Management
files = os.listdir('derived')

def listbox_callback(event, ax, axc):
    if event.keysym == 'Return':
        idx = listbox.curselection()
        filename = listbox.get(idx)
        add_variable(filename, ax, axc, axname)


scrollbary = Tk.Scale(master=windowRIGHT, orient=Tk.VERTICAL, from_=0, to=len(files))
scrollbarx = Tk.Scale(master=windowLEFT, orient=Tk.HORIZONTAL, from_=0, to=50)
listbox = Tk.Listbox(master=windowLEFT)
listbox.bind('<Key>', lambda event: listbox_callback(event, ax, axc))

def search_files(text):
    keywords = text.split(" ")
    entries = []
    for f in files:
        all_found = True
        for key in keywords:
            if key not in f:
                all_found = False
        if all_found:
            entries.append(f)
    return entries

def update_listbox(text):
    listbox.delete(0, Tk.END)
    new_entries = search_files(text)
    for n in new_entries:
        listbox.insert(Tk.END, n)


def entry_callback(event):
    text = entry.get()
    update_listbox(text)


entry = Tk.Entry(master=windowLEFT)
entry.bind('<Key>', entry_callback)


for f in files:
    listbox.insert(Tk.END, f)


rootTOP.pack(fill=Tk.BOTH, expand = 1)
rootBOT.pack(fill=Tk.X)

windowLEFT.pack(side=Tk.LEFT, fill=Tk.BOTH, expand=1)
windowRIGHT.pack(side=Tk.RIGHT, fill=Tk.Y)

entry.pack(fill=Tk.X)
listbox.pack(fill=Tk.BOTH, expand = 1)
scrollbary.pack(fill=Tk.Y, expand = 1)
scrollbarx.pack(fill=Tk.X)

scrollbary.config(command=listbox.yview)
scrollbarx.config(command=listbox.xview)
root.geometry('1280x480+0+0')
window.geometry('300x600+1280+0')

Tk.mainloop()