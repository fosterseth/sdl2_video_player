import numpy as np
import scipy.io
import socket
import os
import subprocess

from matplotlib import gridspec
import matplotlib.pyplot as plt
import matplotlib.patches as pat
import matplotlib.animation as animation

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

import tkinter as Tk

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
        # self.resize_ax()
        # self.rect.figure.canvas.draw()

    def on_release(self, event):
        'on release we reset the press data'
        self.press = None
        self.resize_ax()
        self.rect.figure.canvas.draw()

    def disconnect(self):
        'disconnect all the stored connection ids'
        self.rect.figure.canvas.mpl_disconnect(self.cidpress)
        self.rect.figure.canvas.mpl_disconnect(self.cidrelease)
        self.rect.figure.canvas.mpl_disconnect(self.cidmotion)

class MainPlot():
    def __init__(self, parent, filenames):
        self.fig = Figure(figsize=(15, 4))
        gs = gridspec.GridSpec(2, 2, width_ratios=[6, 1], height_ratios=[5, 1])
        gs.update(left=0.01, right=0.99, wspace=0.01)
        self.ax = self.fig.add_subplot(gs[0, 0])

        self.axname = self.fig.add_subplot(gs[0, 1])
        self.axname.set_ylim(bottom=0, top=10.5)
        self.axname.set_xlim(left=0, right=1)
        self.axname.axis('off')

        self.ax.set_title('Simple plot')
        self.ax.set_ylim(bottom=-1, top=0)
        self.ax.set_yticklabels([])
        self.ax.set_yticks([])

        self.axc = self.fig.add_subplot(gs[1, 0])
        self.axc.set_ylim(bottom=0, top=10)
        self.axc.set_yticklabels([])
        self.axc.set_yticks([])

        self.ctrl_bar = self.axc.add_patch(pat.Rectangle((50, 0), 10, 10, color="green"))
        self.dr = DraggableRectangle(self.ctrl_bar, self.ax, self.fig)

        # a tk.DrawingArea
        canvas = FigureCanvasTkAgg(self.fig, master=parent)
        canvas.show()
        canvas.get_tk_widget().pack(side=Tk.TOP, fill=Tk.BOTH, expand=1)

        canvas._tkcanvas.pack(side=Tk.TOP, fill=Tk.BOTH, expand=1)
        canvas.mpl_connect('button_press_event', lambda event: self.onclick(event, self.ax))

        self.offset = 30.97 - 30
        self.colors = ["#4542f4", "#41f465", "#f44141", "#f441e5"]

        self.loaddata(filenames)

    def draw_rects(self, data, bottom):
        ax = self.ax
        values = data[:, 2].astype(int)
        prev_off = -1
        all_rects = []
        for i in range(0, np.size(data[:, 0])):
            dur = data[i, 1] - data[i, 0]
            if dur > 0:
                # if rect overlaps, then draw at half height so that both show up
                if data[i, 1] < prev_off:
                    height = 5
                else:
                    height = 10
                prev_off = data[i, 1]
                patch = ax.add_patch(pat.Rectangle((data[i, 0], bottom), dur, height, color=self.colors[values[i] - 1]))
                all_rects.append(patch)

    def load_matfile(self, filename):
        return scipy.io.loadmat(filename)['sdata'][0][0][1]

    def loaddata(self, filenames):
        for f in filenames:
            self.add_variable(f)

    def onclick(self, event, axID):
        if event.inaxes == axID:
            print('button=%d, x=%d, y=%d, xdata=%f, ydata=%f' %
                  (event.button, event.x, event.y, event.xdata, event.ydata))
            command = "seekto " + str(event.xdata + self.offset)
            sock.sendto(command.encode(), server_address)


            # add_variable("cevent_eye_roi_child.mat", ax, axc, axname)
        # def __del__(self):
        #     canvas.get_tk_widget().destroy()
        #     plt.close(fig)

    def add_variable(self, filename):
        print(filename)
        ax = self.ax
        axname = self.axname
        axc = self.axc
        bot, top = ax.get_ylim()
        data = self.load_matfile("c:/users/sbf/Desktop/derived/" + filename)
        rects = self.draw_rects(data, top)
        if bot < 0:
            ax.set_ylim(bottom=0, top=10.5)
            axname.set_ylim(bottom=0, top=10.5)
            top = 10.5
        else:
            ax.set_ylim(top=top + 10.5)
            axname.set_ylim(top=top + 10.5)
            top = top + 10.5
        ax.set_xlim(left=data[0, 0], right=data[-1, 1])
        axc.set_xlim(left=data[0, 0], right=data[-1, 1])
        box = axname.add_patch(pat.Rectangle((0, top - 8), 1, 4, color="orange"))
        text = axname.text(0, top - 6.5, filename)
        ax.figure.canvas.draw()



class App(Tk.Tk):
    def __init__(self):
        Tk.Tk.__init__(self)
        self.initFrames()
        self.initWidgets()
        self.favorites = ['cevent_eye_roi_child.mat', 'cevent_eye_roi_parent.mat', 'cevent_inhand_child.mat',
                     'cevent_inhand_parent.mat', 'cevent_eye_joint-attend_both.mat',
                     'cevent_speech_naming_local-id.mat', 'cevent_speech_utterance',
                     'cevent_trials.mat']
        self.selected_files = []
        self.container = None
        subprocess.Popen(["c:/users/sbf/Desktop/WORK/main2/Debug/main2.exe", "7999"])

    def initFrames(self):
        self.wm_title("Embedding in TK")
        self.rootTOP = Tk.Frame(master=self)
        self.rootMIDDLE1 = Tk.Frame(master=self, bg="green")
        self.rootMIDDLE2 = Tk.Frame(master=self, bg="black")
        self.rootBOT = Tk.Frame(master=self, bg="blue")

        self.rootTOP.pack(fill=Tk.X)
        self.rootMIDDLE1.pack(fill=Tk.X)
        self.rootMIDDLE2.pack(fill=Tk.BOTH, expand=1)
        self.rootBOT.pack(fill=Tk.X)

    def initWidgets(self):
        self.buttonQuit = Tk.Button(master=self.rootTOP, text='Quit', command=self.quitapp)
        self.buttonPlay = Tk.Button(master=self.rootTOP, text='Play', command=self.playvideos)
        self.buttonPause = Tk.Button(master=self.rootTOP, text='Pause', command=self.pausevideos)
        self.buttonClearPlot = Tk.Button(master=self.rootTOP, text='ClearPlot', command=self.clearplot)
        self.buttonOpenSubject = Tk.Button(master=self.rootTOP, text='OpenSubject', command=self.opensubject)
        self.buttonOpenVideos = Tk.Button(master=self.rootTOP, text='OpenVideos', command=self.openvideos)

        self.scrollbary = Tk.Scale(master=self.rootMIDDLE2, orient=Tk.VERTICAL, from_=0, to=100)
        self.scrollbarx = Tk.Scale(master=self.rootBOT, orient=Tk.HORIZONTAL, from_=0, to=50)
        self.listbox = Tk.Listbox(master=self.rootMIDDLE2)
        self.listbox.bind('<Key>', self.listbox_callback)

        self.buttonOpenSubject.pack(side=Tk.LEFT)
        self.buttonOpenVideos.pack(side=Tk.LEFT)
        self.buttonPlay.pack(side=Tk.LEFT)
        self.buttonPause.pack(side=Tk.LEFT)
        self.buttonClearPlot.pack(side=Tk.LEFT)
        self.buttonQuit.pack(side=Tk.LEFT)

        self.entry = Tk.Entry(master=self.rootMIDDLE1)
        self.entry.bind('<Key>', self.entry_callback)
        self.entry.pack(fill=Tk.X)

        self.listbox.pack(side=Tk.LEFT, fill=Tk.BOTH, expand = 1)
        self.scrollbary.pack(side=Tk.LEFT, fill=Tk.Y)
        self.scrollbarx.pack(fill=Tk.X)
        self.scrollbary.config(command=self.listbox.yview)
        self.scrollbarx.config(command=self.listbox.xview)

    def initPlot(self):
        self.destroycontainer()
        self.container = Tk.Toplevel(master=self.rootTOP)
        self.mainplot = MainPlot(self.container, self.selected_files)
        self.mainplot.dr.connect()
        self.container.update()
        aw = self.container.winfo_width()
        ah = self.container.winfo_height()
        self.container.geometry('%dx%d+400+0' % (aw, ah))

    def loop(self):
        self.selected_files = ["cevent_trials.mat", "cevent_eye_roi_child.mat"]
        self.initPlot()
        self.clearplot()
        self.after(1000, self.loop)

    def clearplot(self):
        sock.sendto("break".encode(), server_address)
        self.destroycontainer()
        self.resetApp()

    def resetApp(self):
        self.container = None
        self.selected_files = []
        self.listbox.delete(0,Tk.END)

    def destroycontainer(self):
        if self.container != None:
            self.container.destroy()

    def search_files(self, text):
        keywords = text.split(" ")
        entries = []
        for f in self.files:
            all_found = True
            for key in keywords:
                if key not in f:
                    all_found = False
            if all_found:
                entries.append(f)
        return entries

    def update_listbox(self, text):
        self.listbox.delete(len(self.favorites) + 2, Tk.END)
        new_entries = self.search_files(text)
        for n in new_entries:
            self.listbox.insert(Tk.END, n)

    def listbox_callback(self, event):
        if event.keysym == 'Return':
            idx = self.listbox.curselection()
            filename = self.listbox.get(idx)
            self.selected_files.append(filename)
            self.initPlot()

    def entry_callback(self, event):
        text = self.entry.get()
        self.update_listbox(text)

    def openvideos(self):
        command = "open c:/users/sbf/Desktop/cam01.mov 500 500"
        sock.sendto(command.encode(), server_address)
        command = "open c:/users/sbf/Desktop/cam02.mov 1000 500"
        sock.sendto(command.encode(), server_address)

    def opensubject(self):
        self.rootdir = "C:/users/sbf/Desktop/derived"
        self.files = os.listdir(self.rootdir)
        setfiles = set(self.files)
        self.filtered_favorites = list(setfiles.intersection(self.favorites))

        self.listbox.insert(Tk.END, "== FAVORITES ==")
        for i in self.filtered_favorites:
            self.listbox.insert(Tk.END, i)
        self.listbox.insert(Tk.END, "===============")

        for i in self.files:
            self.listbox.insert(Tk.END, i)

        self.scrollbary.config(to=len(self.files))

    def pausevideos(self):
        sock.sendto("pause".encode(), server_address)
        return

    def playvideos(self):
        sock.sendto("play".encode(), server_address)
        return

    def quitapp(self):
        sock.sendto("break".encode(), server_address)
        self.quit()
        self.destroy()


if __name__ == "__main__":

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_address = ('localhost', 7999)

    app = App()
    app.geometry('400x600+0+0')
    app.mainloop()