$fn=40;

beam();

module beam(){{
    difference(){
    union(){
    translate([2,2,0])cylinder(d=4,h=90);
    translate([0,0,0])cube([10,2,90]);
        translate([8,2,0])cylinder(d=4,h=90);
    //translate([0,2,0])cube([2,8,90]);
        
    translate([5,7,0])cylinder(d=10,h=4);
    translate([0,2,0])cube([10,5,4]);
    //translate([2,2,0])cube([3,8,4]);
        

        
 //   translate([5,5,32])cylinder(d=10,h=32);
//    translate([2,2,32])cube([8,3,32]);
//    translate([2,2,32])cube([3,8,32]);    
        
    translate([5,7,86])cylinder(d=10,h=4);
    translate([0,2,86])cube([10,5,4]);  
        
    }
    translate([5,7,0])cylinder(d=2.5,h=10);
    translate([5,7,85])cylinder(d=2.5,h=10);
}
}
}
