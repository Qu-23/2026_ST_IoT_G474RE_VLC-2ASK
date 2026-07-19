function crc16(data){
  let c=0xFFFF;
  for(let b of data){
    c^=(b<<8);
    for(let j=0;j<8;j++){
      if(c&0x8000)
        c=((c<<1)^0x1021)&0xFFFF;
      else
        c=(c<<1)&0xFFFF;
    }
  }
  return c;
}

let A=[0x02,0x04,0x54,0x65,0x73,0x74];
let B=[0x02,0x05,0x20,0x54,0x65,0x73,0x74];
let C=[0x02,0x05,0x54,0x65,0x73,0x74,0x00];
let D=[0x02,0x05,0x54,0x65,0x73,0x74,0x0D];
let E=[0x02,0x05,0x54,0x65,0x73,0x74,0x0A];

function hex4(v){return "0x"+v.toString(16).toUpperCase().padStart(4,'0');}

console.log("A (no space):  "+hex4(crc16(A)));
console.log("B (with space): "+hex4(crc16(B)));
console.log("C (len5+null):  "+hex4(crc16(C)));
console.log("D (len5+CR):    "+hex4(crc16(D)));
console.log("E (len5+LF):    "+hex4(crc16(E)));
console.log("");
console.log("TX=0x6EFE? A:"+(crc16(A)===0x6EFE)+" B:"+(crc16(B)===0x6EFE)+" C:"+(crc16(C)===0x6EFE)+" D:"+(crc16(D)===0x6EFE)+" E:"+(crc16(E)===0x6EFE));
console.log("Correct=0xA01E? A:"+(crc16(A)===0xA01E));

// Reference check: CRC-16/CCITT-FALSE for "123456789" should be 0x29B1
let ref=[0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39];
console.log("\nRef check (123456789): "+hex4(crc16(ref))+" (should be 0x29B1 for CCITT-FALSE)");
