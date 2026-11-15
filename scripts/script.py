import math
import sys
import time

import api as ship

@ship.on("console_message")
def on_message(message):
    xx, yy = message.split(" ")
    x = float(xx)
    y = float(yy)
    FlyTo(x, y)




ship.start()

propulsion = ship.Propulsion("propulsion_0")
storage = ship.Storage("storage_1")
rcs = ship.RCS("rcs_2")
drill = ship.Drill("drill_3")
scan = ship.Scaner("scaner_4")
power = ship.Power("power_5")
factory = ship.Factory("factory_6")

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
    rcs.gyro(0)
        


def LookAt(x, y):
    pos_x, pos_y = ship.getPos()
    diff_x = x - pos_x
    diff_y = y - pos_y
    RotateTowards(diff_x, diff_y)

def FlyTo(x, y):
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
    
print(factory.getRecipies())

while ship._running:
    time.sleep(0.01)