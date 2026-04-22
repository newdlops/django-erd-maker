from django.db import models

from .base import BaseRecord


class Product(BaseRecord):
    sku = models.CharField(max_length=32)

    class Meta:
        db_table = "catalog_product_entity"
