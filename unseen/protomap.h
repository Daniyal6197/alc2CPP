#ifndef _PROTOMAP_H_
#define _PROTOMAP_H_

/*
 * initialize the protomap for from `file'
 */
void protomap_init(char *file);

/*
 * classify protocol numbers according to `proto'
 */
char *protomap_classify(unsigned char proto);

/*
 * free storage associated with protomap
 */
void protomap_free();

/*
 * dump contents of protomap
 */
void protomap_dump();

/*
 * files containing the definitions
 */
#define PROTO_FILE "protocols/protocol.list"

#endif /* _PROTOMAP_H_ */
