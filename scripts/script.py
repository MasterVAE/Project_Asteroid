import math
import sys
import time

import api as ship

state = "mine"

@ship.on("console_message")
def on_message(message):
    global state
    msg = message.split(" ")
    if msg[0] == "goto":
        FlyTo(float(msg[1]), float(msg[2]))
    if msg[0] == "get":
        if msg[1] == "pos":
            print(ship.getPos())
        elif msg[1] == "vel":
            print(ship.getVel())
        elif msg[1] == "fuel":
            print(f"fuel {int(tank.getFuel()/tank.getMaxFuel() * 100)}%")
        elif msg[1] == "inventory":
            print(storage.getStorage())

    if msg[0] == "refuel":
        state = "refuel"
    if msg[0] == "mine":
        state = "mine"
    if msg[0] == "exit":
        state = "exit"



ship.start()

propulsion = ship.Propulsion("propulsion_0")
storage = ship.Storage("storage_0")
rcs = ship.RCS("rcs_0")
drill = ship.Drill("drill_0")
scan = ship.Scanner("scanner_0")
power = ship.Power("power_0")
tank = ship.FuelTank("fuel_tank_0")

PI = math.pi

propulsion.throttle(0)
rcs.setThrust((0, 0))

def Forward():
    angle = ship.getAngle()
    return (math.cos(angle), math.sin(angle))

def Right():
    angle = ship.getAngle()
    return (math.sin(angle), -math.cos(angle))

def dotProduct(x, y):
    return x[0] * y[0] + x[1] * y[1]

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
    print("slow approach")
    propulsion.throttle(0)
    rcs.gyro(0)
    max_thrust = rcs.getThrust()
    try:
        while ship.is_running():
            pos_x, pos_y = ship.getPos()
            vx, vy = ship.getVel()
            dir_x, dir_y = x - pos_x, y - pos_y
            dist = math.hypot(dir_x, dir_y)
            if dist <= 0.25 and math.hypot(vx, vy) <= 0.05:
                break

            max_acc = max_thrust / ship.getMass()
            # Slow down near the target and keep correcting lateral drift.
            speed = min(3.0, math.sqrt(2 * max_acc * dist), dist)
            target_vx = dir_x / dist * speed if dist > 0 else 0.0
            target_vy = dir_y / dist * speed if dist > 0 else 0.0
            thrust_x = (target_vx - vx) / (0.5 * max_acc)
            thrust_y = (target_vy - vy) / (0.5 * max_acc)

            angle = ship.getAngle()
            cos_angle, sin_angle = math.cos(angle), math.sin(angle)
            # RCS expects one vector in local coordinates: X forward, Y left.
            rcs.setThrust((
                cos_angle * thrust_x + sin_angle * thrust_y,
                -sin_angle * thrust_x + cos_angle * thrust_y,
            ))
            time.sleep(0.02)
    finally:
        if ship.is_running():
            rcs.setThrust((0, 0))

def Stop():
    x, y = ship.getVel();
    RotateTowards(-x, -y)
    propulsion.throttle(1)

    vel = ship.getVel()
    while vel[0]**2 + vel[1]**2 > 0.1:
        time.sleep(0.01)
        vel = ship.getVel()
    propulsion.throttle(0)


def FlyTo(x, y):

    pos = ship.getPos()
    if(math.sqrt((x-pos[0])**2 + (y - pos[1])**2) < 50):
        FlyToSlow(x, y)
        return

    print("flying")

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

    while dotProduct(vel, Forward()) < 0:
        time.sleep(0.01)
        vel = ship.getVel()

    propulsion.throttle(0)

def Mine():
    target = (0, 0)
    for obj in scan.scan():
        if obj['type'] == 'asteroid':
            target = (obj['x'], obj['y'])
            break
    print("target: ", target)
    FlyTo(target[0], target[1])

    print("drilling")
    drill.enable()
    time.sleep(10)
    drill.disable()

def Refuel():
    global state
    print(f"go for refueling, fuel: {int(tank.getFuel()/tank.getMaxFuel() * 100)}%")
    FlyTo(0, 0)

    strg = storage.getStorage()

    for key in strg:
        for i in range(strg[key]):
            storage.transfer(0, key)

    print(f"fuel: {int(tank.getFuel()/tank.getMaxFuel() * 100)}%")

    storage.readyToRecieve()
    scan.signal(0, "need fuel")
    time.sleep(10)
    storage.notReadyToRecieve()

    print(f"refueled {int(tank.getFuel()/tank.getMaxFuel() * 100)}%")
    state = "mine"


while ship._running:
    if tank.getFuel() < 0.5 * tank.getMaxFuel():
        state = "refuel"

    if state == "mine":
        Mine()
    elif state == "refuel":
        Refuel()
    elif state == "exit":
        break
    time.sleep(0.01)
