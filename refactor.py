import numpy as np
import scipy.io
import socket
import os
import subprocess
import win32api

from matplotlib import gridspec
import matplotlib.pyplot as plt
import matplotlib.patches as pat

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

import tkinter as Tk


class Drag:
    def __init__(self, rectmain, rectedge, canvas1, mainplot):
        self.canvas = canvas1
        self.rectmain = rectmain
        self.rectedge = rectedge
        self.mainplot = mainplot

        self.canvas.bind("<ButtonRelease-1>", self.released)
        self.canvas.bind("<Button-1>", self.rect_pressed)
        self.canvas.tag_bind(self.rectmain, "<B1-Motion>", self.rectmain_motion)
        self.canvas.tag_bind(self.rectedge, "<B1-Motion>", self.rectedge_motion)
        self.canvas.tag_bind(self.rectedge, "<Enter>", self.cursor_arrow)
        self.canvas.tag_bind(self.rectedge, "<Leave>", self.cursor_normal)
        self.press = None

    def released(self, event):
        x0, y0, x1, y1 = self.canvas.coords(self.rectmain)
        can_width = self.canvas.winfo_width()
        x0_norm = x0/can_width
        x1_norm = x1/can_width
        self.mainplot.update_axes(x0_norm, x1_norm)

    def rect_pressed(self, event):
        x0, y0, x1, y1 = self.canvas.coords(self.rectmain)
        x2, y0, x3, y1 = self.canvas.coords(self.rectedge)
        can_width = self.canvas.winfo_width()
        if event.x < x0:
            resize = 1
        elif event.x > x1:
            resize = 2
        else:
            resize = 0
        self.press = resize, x0, x1, x2, x3, y0, y1, event.x, can_width

    def cursor_normal(self, event):
        self.canvas.config(cursor="arrow")

    def cursor_arrow(self, event):
        self.canvas.config(cursor="sb_h_double_arrow")

    def rectmain_motion(self, event):
        if self.press is None:
            return
        resize, x0, x1, x2, x3, y0, y1, xpress, can_width = self.press
        dx = event.x - xpress
        newx2 = dx + x2
        newx3 = dx + x3
        if (newx2 > 0) & (newx3 < can_width):
            self.canvas.coords(self.rectmain, (x0+dx, y0, x1+dx, y1))
            self.canvas.coords(self.rectedge, (newx2, y0, newx3, y1))

    def rectedge_motion(self, event):
        if self.press is None:
            return
        resize, x0, x1, x2, x3, y0, y1, xpress, can_width = self.press
        dx = event.x - xpress
        if resize == 1:
            newx2 = dx + x2
            if ((x1-newx2) > 5) & (newx2 > 0):
                self.canvas.coords(self.rectmain, (x0+dx, y0, x1, y1))
                self.canvas.coords(self.rectedge, (newx2, y0, x3, y1))
        elif resize == 2:
            newx3 = dx + x3
            if ((newx3 - x0) > 5) & (newx3 < can_width):
                self.canvas.coords(self.rectmain, (x0, y0, x1+dx, y1))
                self.canvas.coords(self.rectedge, (x2, y0, newx3, y1))


class MainPlot():
    def __init__(self, parent, filenames, trials, timing, destroy_fun, mainplot_axes_fun, sock, offset_frames):
        self.mainplot_axes_fun = mainplot_axes_fun
        self.sock = sock
        self.fig = Figure(figsize=(12, 4))
        self.destroy_fun = destroy_fun
        gs = gridspec.GridSpec(1, 1)
        gs.update(left=0.001, right=0.999, bottom=0.07, top=0.999)
        self.ax = self.fig.add_subplot(gs[0,0])

        self.fig2 = Figure(figsize=(2,4))
        self.axname = self.fig2.add_subplot(gs[0,0])
        # self.axname = self.fig.add_subplot(gs[0, 1])
        self.axname.set_ylim(bottom=0, top=10.5)
        self.axname.set_xlim(left=0, right=1)
        self.axname.axis('off')

        self.ax.set_ylim(bottom=-1, top=0)
        self.ax.set_yticklabels([])
        self.ax.set_yticks([])

        # a tk.DrawingArea
        canvas = FigureCanvasTkAgg(self.fig, master=parent)
        canvas.get_tk_widget().config(highlightthickness=0)
        canvas.show()
        canvas.get_tk_widget().grid(row=1,column=0, sticky='NSEW')
        canvas.mpl_connect('button_press_event', self.onclick)

        canvas2 = FigureCanvasTkAgg(self.fig2, master=parent)
        canvas2.get_tk_widget().config(highlightthickness=0)
        canvas2.show()
        canvas2.get_tk_widget().grid(row=1,column=1, sticky='NSEW')
        canvas2.mpl_connect('button_press_event', self.onclick)

        camtime = self.get_camTime(timing)
        camrate = self.get_camRate(timing)
        self.offset = (offset_frames / camrate) - camtime
        print(self.offset)
        self.colors = ["#4542f4", "#41f465", "#f44141", "#f441e5"]
        self.label_colors = ["#E9CAF4", "#CAEDF4"]
        self.numstreams = 0

        data = self.load_matfile(trials)
        # self.xmin = float('Inf')
        # self.xmax = -float('Inf')
        self.xmin = data[0,0]
        self.xmax = data[-1,1]
        # self.update_axes(0,1)
        self.boxes_and_labels = []
        self.loaddata(filenames)

    def get_camTime(self, timingfile):
        return scipy.io.loadmat(timingfile)['trialInfo']['camTime'][0][0][0][0]

    def get_camRate(self, timingfile):
        return scipy.io.loadmat(timingfile)['trialInfo']['camRate'][0][0][0][0]

    def cstream2cevent(self, cstream):
        cevent = np.array([[-1, -1, -1]], dtype=np.float64)
        numrows, numcols = np.shape(cstream)
        if numcols > 2:
            return cstream
        in_event = False
        prev_category = 0
        build_event = np.array([[-1, -1, -1]], dtype=np.float64)
        for c in range(0, numrows):
            cur_category = cstream[c, 1]
            if cur_category != prev_category:
                if build_event[0, 0] > 0:
                    build_event[0, 1] = cstream[c, 0]
                    build_event[0, 2] = prev_category
                    cevent = np.append(cevent, build_event, axis=0)
                    build_event = np.array([[-1, -1, -1]])
                elif cur_category > 0:
                    build_event[0, 0] = cstream[c, 0]
            prev_category = cur_category
        return cevent[1:, :]

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

    def onclick(self, event):
        if event.inaxes == self.ax:
            # print('button=%d, x=%d, y=%d, xdata=%f, ydata=%f' %
            #       (event.button, event.x, event.y, event.xdata, event.ydata))
            command = "seekto " + str(event.xdata + self.offset)
            self.sock.send(command.encode())
        if event.inaxes == self.axname:
            for b in self.boxes_and_labels:
                box, label = b
                contains, attrd = box.contains(event)
                if contains:
                    self.destroy_fun(label)

    def add_variable(self, filename):
        print(filename)
        self.numstreams += 1
        ax = self.ax
        axname = self.axname
        # axc = self.axc
        bot, top = ax.get_ylim()
        data = self.load_matfile(filename)
        data = self.cstream2cevent(data)
        rects = self.draw_rects(data, top)
        if bot < 0:
            ax.set_ylim(bottom=0, top=10.5)
            axname.set_ylim(bottom=0, top=10.5)
            top = 10.5
        else:
            ax.set_ylim(top=top + 10.5)
            axname.set_ylim(top=top + 10.5)
            top = top + 10.5
        box = axname.add_patch(pat.Rectangle((0, top - 10.5), 1, 10, color=self.label_colors[self.numstreams % 2]))
        filenamesplit = filename.split('/')

        text = axname.text(0, top - 6.5, filenamesplit[-1])
        self.boxes_and_labels.append((box,filename))
        ax.figure.canvas.draw()
        axname.figure.canvas.draw()

    def update_axes(self, xlim1, xlim2):
        width = self.xmax - self.xmin
        newleft = xlim1 * width + self.xmin
        newright = xlim2 * width + self.xmin
        self.ax.set_xlim(left=newleft, right=newright)
        self.mainplot_axes_fun(newleft, newright)
        self.ax.figure.canvas.draw()

    def get_axes(self):
        return self.ax.get_xlim()


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
        self.formats = ["mov", "mp4", "wmv", "mpeg4", "h264"]
        self.container = None
        self.connect()

        self.bar_x0x3 = (0,70)
        self.videopos = 500
        self.mainplot_axes = (0,1)
        self.canvas2width = 0
        # self.multidirroot = self.find_multiwork_path()
        self.multidirroot = "c:/users/sbf/Desktop/multiwork/"
        self.subpaths = self.parse_subject_table()
        self.cur_subject = ""
        self.showing_variables = False
        # self.loop() #check for memory leakage

    def initFrames(self):
        self.wm_title("Embedding in TK")
        self.rootTOP = Tk.Frame(master=self)
        self.rootMIDDLE1 = Tk.Frame(master=self, bg="green")
        self.rootEntry = Tk.Frame(master=self, bg="red")
        self.rootMIDDLE2 = Tk.Frame(master=self, bg="black")
        self.rootBOT = Tk.Frame(master=self, bg="blue")

        self.rootTOP.pack(fill=Tk.X)
        self.rootEntry.pack(fill=Tk.X)
        self.rootMIDDLE1.pack(fill=Tk.X)
        self.rootMIDDLE2.pack(fill=Tk.BOTH, expand=1)
        self.rootBOT.pack(fill=Tk.X)

    def initWidgets(self):
        self.buttonQuit = Tk.Button(master=self.rootTOP, text='Quit', command=self.quitapp)
        self.buttonPlay = Tk.Button(master=self.rootTOP, text='Play', command=self.playvideos)
        self.buttonPause = Tk.Button(master=self.rootTOP, text='Pause', command=self.pausevideos)
        self.buttonClearPlot = Tk.Button(master=self.rootTOP, text='ClearPlot', command=self.clearplot)

        self.scrollbary = Tk.Scale(master=self.rootMIDDLE2, orient=Tk.VERTICAL, from_=0, to=100)
        self.scrollbarx = Tk.Scale(master=self.rootBOT, orient=Tk.HORIZONTAL, from_=0, to=50)
        self.listbox = Tk.Listbox(master=self.rootMIDDLE2)
        self.listbox.bind('<Key>', self.listbox_callback)

        self.label_subject = Tk.Label(master=self.rootEntry, text="Enter SubjectID or Path")
        self.entry_subject_str = Tk.StringVar()
        self.entry_subject_str.trace("w", self.entry_subject_str_callback)
        self.entry_subject = Tk.Entry(master=self.rootEntry, textvariable=self.entry_subject_str)
        self.entry_subject.bind('<Key>', self.entry_subject_callback)

        self.entry = Tk.Entry(master=self.rootMIDDLE1)
        self.entry.bind('<Key>', self.entry_callback)
        self.label_variable = Tk.Label(master=self.rootMIDDLE1, text="Enter Variable")

        # self.buttonOpenSubject.pack(side=Tk.LEFT)
        self.buttonPlay.pack(side=Tk.LEFT)
        self.buttonPause.pack(side=Tk.LEFT)
        self.buttonClearPlot.pack(side=Tk.LEFT)
        self.buttonQuit.pack(side=Tk.LEFT)

        self.label_variable.pack(side=Tk.LEFT)
        self.entry.pack(side=Tk.LEFT, fill=Tk.X, expand = 1)


        self.listbox.pack(side=Tk.LEFT, fill=Tk.BOTH, expand = 1)
        self.scrollbary.pack(side=Tk.LEFT, fill=Tk.Y)
        self.scrollbarx.pack(fill=Tk.X)
        self.scrollbary.config(command=self.listbox.yview)
        self.scrollbarx.config(command=self.listbox.xview)

        self.label_subject.pack(side=Tk.LEFT, fill=Tk.X)
        self.entry_subject.pack(side=Tk.LEFT, fill=Tk.X, expand=1)


    def initPlot(self):
        self.destroycontainer()
        self.container = Tk.Toplevel(master=self.rootTOP, bg="white")
        self.container.bind('<Key>', self.root_keypress)

        # self.container_frameL = Tk.Frame(master=self.container)
        # self.container_frameR = Tk.Frame(master=self.container, bg="white")

        self.canvas = Tk.Canvas(master=self.container, bg="orange", height = 60, highlightthickness=0)
        self.rectouter = self.canvas.create_rectangle(self.bar_x0x3[0], 0, self.bar_x0x3[1], 60, fill="black")
        self.rectinner = self.canvas.create_rectangle(self.bar_x0x3[0]+10, 0, self.bar_x0x3[1]-10, 60, fill="red")
        # self.canvas.addtag_all("all")

        self.canvas2 = Tk.Canvas(master=self.container, bg="cyan", height=10, highlightthickness=0)
        # self.canvas2.addtag_all("all")
        self.rect_playback = self.canvas2.create_rectangle(50,0,60,10, fill="black")
        # self.canvas2.pack(fill=Tk.X, expand=1)
        self.canvas2.grid(row=0,column=0, sticky='EW')
        self.mainplot = MainPlot(self.container, self.selected_files, self.rootdir + "derived/cevent_trials.mat", self.rootdir+ "derived/timing.mat", self.destroymainplot, self.mainplot_axes_fun, self.sock, self.offset_frame)
        self.dr = Drag(self.rectinner, self.rectouter, self.canvas, self.mainplot)

        self.canvas.grid(row=2,column=0, sticky='EW')

        self.container.grid_columnconfigure(0, weight=1)
        self.container.grid_columnconfigure(1, weight=1, minsize=200)
        self.container.grid_rowconfigure(0, weight=1)
        self.container.grid_rowconfigure(1, weight=30)
        self.container.grid_rowconfigure(2, weight=1)

        self.container.update()
        aw = self.container.winfo_width()
        ah = self.container.winfo_height()
        self.container.geometry('%dx%d+400+0' % (aw, ah))
        self.dr.released(None)
        self.rect_playback_pos()

    def get_offset_frame(self, subpath):
        offset = 0
        extra_p_folder = subpath + 'extra_p/'
        supp_files_folder = subpath + 'supporting_files/'
        er = ""
        filefound = False
        if os.path.exists(extra_p_folder):
            files = os.listdir(extra_p_folder)
            for f in files:
                if f == "extract_range.txt":
                    er = extra_p_folder + f
                    filefound = True
                    break

        if not filefound:
            if os.path.exists(supp_files_folder):
                files = os.listdir(supp_files_folder)
                for f in files:
                    if f == "extract_range.txt":
                        er = supp_files_folder + f
                        filefound = True
                        break
        if filefound:
            fid = open(er, 'r')
            line = fid.readline()
            offset = int(line[1:-2])
            fid.close()
        return offset

    def find_multiwork_path(self):
        drives = win32api.GetLogicalDriveStrings()
        drives = drives.split('\000')[:-1]
        multiwork = ""
        for d in drives:
            try:
                info = win32api.GetVolumeInformation(d)
                if info[0] == 'multiwork':
                    multiwork = d
                    break
            except:
                print("could not get info for this drive")
        return multiwork


    def root_keypress(self, event):
        # print(event.keysym)
        if event.keysym == 'space':
            self.sock.send("toggleplay".encode())

    def connect(self):
        port = 50001
        self.serverprocess = subprocess.Popen(["c:/users/sbf/Desktop/videoserver/main2.exe", str(port)])
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = ('127.0.0.1', 50001)
        self.sock.connect(server_address)

    def parse_subject_table(self):
        subpaths = []
        fn = self.multidirroot + "subject_table.txt"
        if os.path.exists(fn):
            fid = open(self.multidirroot + "subject_table.txt", "r")
            lines = fid.readlines()
            subpaths = []
            for line in lines:
                line = line.split("\n")[0]
                subpath = line.split("\t")[0:4]
                subpaths.append(subpath)
            fid.close()
        return subpaths

    def mainplot_axes_fun(self, left, right):
        self.mainplot_axes = (left, right)

    def rect_playback_pos(self):
        if self.container != None:
            self.sock.send("gettime".encode())
            rec = self.sock.recv(20)
            secs = float(rec) - self.mainplot.offset
            x,y = self.mainplot_axes
            if (y > x):
                secs_norm = (secs-x) / (y - x)
                canvas2width = self.canvas2.winfo_width()
                newx = secs_norm * canvas2width
                # print(x, y, secs, self.canvas2width, newx)
                self.canvas2.coords(self.rect_playback, newx, 0, newx+10, 10)
            self.after(100, self.rect_playback_pos)

    def loop(self):
        self.selected_files = ["cevent_trials.mat", "cevent_eye_roi_child.mat"]
        self.initPlot()
        self.destroymainplot("cevent_trials.mat")
        self.clearplot()
        self.after(1000, self.loop)

    def closeserver(self):
        if self.sock != None:
            self.sock.send("break".encode())
            stream = self.serverprocess.communicate()[0]
            rc = self.serverprocess.returncode
            print(rc)

    def clearplot(self):
        self.closeserver()
        self.destroycontainer()
        self.resetApp()

    def resetApp(self):
        self.container = None
        self.selected_files = []
        self.listbox.delete(0,Tk.END)
        self.connect()

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

    def insert_videos_and_favorites(self):
        self.listbox.delete(0,Tk.END)
        self.listbox.insert(Tk.END, "== VIDEOS ==")
        for v in self.videolist:
            self.listbox.insert(Tk.END, v)

        self.listbox.insert(Tk.END, "== FAVORITES ==")
        for i in self.filtered_favorites:
            self.listbox.insert(Tk.END, i)
        self.listbox.insert(Tk.END, "===============")

        for i in self.files:
            self.listbox.insert(Tk.END, i)

        self.showing_variables = True

    def update_listbox(self, text):
        if self.showing_variables is False:
            self.insert_videos_and_favorites()
            self.showing_variables = True
        self.listbox.delete(len(self.favorites) + len(self.videolist) + 2, Tk.END)
        new_entries = self.search_files(text)
        for n in new_entries:
            self.listbox.insert(Tk.END, n)
        self.scrollbary.config(to=len(new_entries))

    def construct_subpath_from_listbox(self, text):
        subpath = ""
        if len(text) > 0:
            linesplit = text.split("    ")
            subpath = self.multidirroot + "experiment_" + linesplit[1] + "/included/" + "__" + linesplit[2] + "_" + linesplit[3] + "/"
        return subpath

    def listbox_callback(self, event):
        if event.keysym == 'Return':
            idx = self.listbox.curselection()
            if self.showing_variables:
                filename = self.listbox.get(idx)
                if filename in self.videolist:
                    self.openvideo(self.rootdir + filename)
                else:
                    self.selected_files.append(self.rootdir + "derived/"+ filename)
                    if self.container == None:
                        self.initPlot()
                    else:
                        self.mainplot.add_variable(self.rootdir + "derived/" + filename)
            else:
                subpath_str = self.listbox.get(idx)
                subpath = self.construct_subpath_from_listbox(subpath_str)
                self.check_subject(subpath)


    def search_subjects(self, text):
        entries = []
        for s in self.subpaths:
            subpath = "".join(s)
            if text in subpath:
                entries.append(s)
        return entries

    def update_listbox_with_subjects(self, text):
        if self.showing_variables is True:
            self.showing_variables = False
        self.listbox.delete(0, Tk.END)
        new_entries = self.search_subjects(text)
        for n in new_entries:
            toadd = "    ".join(n)
            self.listbox.insert(Tk.END, toadd)
        self.scrollbary.config(to=len(new_entries))

    def entry_subject_str_callback(self, *args):
        text = self.entry_subject_str.get()
        self.update_listbox_with_subjects(text)


    def entry_subject_callback(self, event):
        if event.keysym == 'Return':
            text = self.listbox.get(0)
            subpath = self.construct_subpath_from_listbox(text)
            self.check_subject(subpath)

    def entry_callback(self, event):
        text = self.entry.get()
        self.update_listbox(text)

    def openvideo(self, filename):
        command = "open " + filename + " " + str(self.videopos)+ " 500"
        self.videopos += 50
        self.sock.send(command.encode())

    # def get_root_dir(self, subpath):
    #     if os.path.isdir(text):
    #         subpath = text
    #     else:
    #         subpath = self.get_subject_path(text)
    #         if len(subpath) is 0:
    #             self.entry_subject.delete(0, Tk.END)
    #             self.entry_subject.insert(0,"invalid subject")
    #     return subpath


    def check_subject(self, subpath):
        if len(subpath) > 0:
            if subpath[-1] != '/':
                subpath = subpath + '/'
        if subpath == self.cur_subject:
            return
        if not os.path.isdir(subpath):
            self.entry_subject_str.set("Invalid Subject")
            return
        if self.cur_subject != "":
            self.clearplot()
        self.opensubject(subpath)

    def opensubject(self, subpath):
        # self.rootdir = "C:/users/sbf/Desktop/7001/"
        # self.rootdir = self.get_root_dir()

        self.rootdir = subpath
        if len(self.rootdir) is 0:
            return
        self.offset_frame = self.get_offset_frame(subpath)
        self.files = os.listdir(self.rootdir+"derived")
        setfiles = set(self.files)
        self.filtered_favorites = list(setfiles.intersection(self.favorites))

        allfolders = os.listdir(self.rootdir)

        self.videolist = []
        for a in allfolders:
            if ("video_r" in a) and (os.path.isdir(self.rootdir + a)):
                videos = os.listdir(self.rootdir + a)
                for v in videos:
                    vsplit = v.split(".")
                    if vsplit[1] in self.formats:
                        self.videolist.append(a + "/" + v)

        self.insert_videos_and_favorites()

        self.scrollbary.config(to=len(self.files))
        self.cur_subject = subpath

    def destroymainplot(self, filename):
        print(filename)
        print(self.selected_files)
        self.selected_files.remove(filename)
        x0,y0,x1,y1 = self.canvas.coords(self.rectouter)
        self.bar_x0x3 = (x0, x1)
        self.destroycontainer()
        self.initPlot()

    def pausevideos(self):
        self.sock.send("pause".encode())
        return

    def playvideos(self):
        self.sock.send("play".encode())
        return

    def quitapp(self):
        self.closeserver()
        self.quit()
        self.destroy()


if __name__ == "__main__":

    app = App()
    app.geometry('400x600+0+0')
    app.mainloop()