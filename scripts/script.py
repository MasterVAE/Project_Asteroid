import math
import sys
import time

import api as ship

@ship.on("console_message")
def on_message(message):
    msg = message.split(" ")
    if msg[0] == "goto":
        FlyTo(float(msg[1]), float(msg[2]))
    if msg[0] == "get":
        if msg[1] == "pos":
            print(ship.getPos())
        elif msg[1] == "vel":
            print(ship.getVel())




ship.start()

propulsion = ship.Propulsion("propulsion_0")
storage = ship.Storage("storage_0")
rcs = ship.RCS("rcs_0")
drill = ship.Drill("drill_0")
scan = ship.Scanner("scanner_0")
power = ship.Power("power_0")
factory = ship.Factory("factory_0")

mass = ship.getMass()

PI = math.pi

def RotateTowards(x, y):
    need_angle = math.atan2(y, x)
    if(need_angle < 0):
        need_angle += 2 * PI
    angle = ship.getAngle()
    diff = need_angle - angle
    while(abs(diff) > 0.1):
        if(diff > PI):
            diff -= 2 * PI
        if(diff < -PI):
            diff += 2 * PI

        if(diff > 0):
            rcs.gyro(1)
        else:
            rcs.gyro(-1)

        time.sleep(0.01)
        angle = ship.getAngle()
        diff = need_angle - angle 
    while(abs(diff) > 0.01):
        if(diff > PI):
            diff -= 2 * PI
        if(diff < -PI):
            diff += 2 * PI      
        if(diff > 0):
            rcs.gyro(0.1)
        else:
            rcs.gyro(-0.1)        
        time.sleep(0.01)
        angle = ship.getAngle()
        diff = need_angle - angle
    rcs.gyro(0)
        


def LookAt(x, y):
    pos_x, pos_y = ship.getPos()
    diff_x = x - pos_x
    diff_y = y - pos_y
    RotateTowards(diff_x, diff_y)

def FlyToSlow(x, y):
    pos_x, pos_y = ship.getPos()
    dir_x = x - pos_x
    dir_y = y - pos_y

    print("accelerating")

    rcs.setThrust((dir_x, dir_y))

    vx, vy = ship.getVel()
    speed_sq = vx**2 + vy**2

    max_thrust = rcs.getThrust()
    pos_x, pos_y = ship.getPos()
    dist = math.sqrt((x - pos_x)**2 + (pos_y - y)**2)

    while(speed_sq < 2 * max_thrust * dist / mass):
        time.sleep(0.01)

        vx, vy = ship.getVel()
        speed_sq = vx**2 + vy**2

        pos_x, pos_y = ship.getPos()
        dist = math.sqrt((x - pos_x)**2 + (pos_y - y)**2)

    vx, vy = ship.getVel()
    rcs.setThrust((-vx, - vy))

    print("stoping")

    speed_sq = vx**2 + vy**2

    while(speed_sq > 0.1):
        time.sleep(0.01)
        vx, vy = ship.getVel()
        speed_sq = vx**2 + vy**2

    rcs.setThrust((0, 0))

def Stop():
    x, y = ship.getVel();
    RotateTowards(-x, -y)
    propulsion.throttle(1)

    vel = ship.getVel()
    while vel[0]**2 + vel[1]**2 > 0.1:
        time.sleep(0.01)
    propulsion.throttle(0)


def FlyTo(x, y):
    Stop()
    LookAt(x, y)
    acc = propulsion.getMaxThrottle() / ship.getMass()
    pos = ship.getPos()
    dist = math.sqrt((pos[0]-x)**2 + (pos[1]-y)**2)
    t = math.sqrt(dist / acc / 2)

    propulsion.throttle(1)
    time.sleep(t)
    propulsion.throttle(0)

    vx, vy = ship.getVel()
    RotateTowards(-vx, -vy)


    vel = ship.getVel()
    speed_sq = vel[0]**2 + vel[1]**2

    pos = ship.getPos()
    dist = math.sqrt((pos[0]-x)**2 + (pos[1]-y)**2)
    while dist > speed_sq / acc / 2:
        time.sleep(0.01)
        pos = ship.getPos()
        dist = math.sqrt((pos[0]-x)**2 + (pos[1]-y)**2)

    propulsion.throttle(1)

    vel = ship.getVel()
    speed = math.sqrt(vel[0]**2 + vel[1]**2)

    while speed > 0.1:
        time.sleep(0.01)
        vel = ship.getVel()
        speed = math.sqrt(vel[0]**2 + vel[1]**2)

    propulsion.throttle(0)

while ship._running:
    time.sleep(0.01)